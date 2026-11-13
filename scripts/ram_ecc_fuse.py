# Reads or changes the SAMD51's RAM ECCDIS fuse (bit 39 of the NVM user row) over SWD with a J-Link.
# Usage: python3 scripts/ram_ecc_fuse.py [status|enable|disable], or restore <backup.bin> to write a saved user row back
#
# Writing the user row means erasing it and writing all 512 bytes back, like ASF's _user_row_write_exec(). The first 32
# bytes hold factory calibration, so this saves a backup of the row first, changes only bit 39 and checks the result.
# The new value takes effect at the next reset. RAM ECC takes the top 128 KB of RAM, so "enable" refuses to run unless
# every stage in flash keeps its stack in the bottom 128 KB (flash a build with `make flash_to_mram` first, so the
# bootloader doesn't restore an older one from the MRAM golden copy).

import argparse
import datetime
import struct
import sys

import pylink  # pip install pylink-square

USER_ROW = 0x00804000
USER_ROW_SIZE = 512
ECCDIS_WORD = 1  # Bit 39 is bit 7 of the second word
ECCDIS_BIT = 1 << 7

NVMCTRL = 0x41004000
NVMCTRL_CTRLA = NVMCTRL + 0x00
NVMCTRL_CTRLB = NVMCTRL + 0x04
NVMCTRL_INTFLAG = NVMCTRL + 0x10
NVMCTRL_STATUS = NVMCTRL + 0x12
NVMCTRL_ADDR = NVMCTRL + 0x14
CMD_EP = 0x00  # Erase page (user and AUX pages only)
CMD_WQW = 0x04  # Write quad word
CMD_PBC = 0x15  # Page buffer clear
CMDEX_KEY = 0xA5 << 8
INTFLAG_ERRORS = 0x2E  # NVME, LOCKE, PROGE, ADDRE

DSU_STATUSB = 0x41002000 + 0x102
RAMECC_STATUS = 0x41020000 + 0x03

# Every stage's initial stack pointer must be in the bottom 128 KB of RAM for RAM ECC: stage 1, the three stage-2 copies
# and PVDXos (bootloader/src/image_format.h)
VECTOR_TABLES = {"stage 1": 0x00000000, "stage 2 copy 0": 0x00002000, "stage 2 copy 1": 0x00006000,
                 "stage 2 copy 2": 0x0000A000, "PVDXos": 0x0001E000}
RAM_ECC_TOP = 0x20020000


def wait_ready(jlink):
    while not jlink.memory_read16(NVMCTRL_STATUS, 1)[0] & 1:
        pass


def nvm_command(jlink, cmd):
    jlink.memory_write16(NVMCTRL_INTFLAG, [0xFFFF])
    jlink.memory_write32(NVMCTRL_CTRLB, [cmd | CMDEX_KEY])
    wait_ready(jlink)
    errors = jlink.memory_read16(NVMCTRL_INTFLAG, 1)[0] & INTFLAG_ERRORS
    if errors:
        raise RuntimeError(f"NVMCTRL command 0x{cmd:02x} failed (INTFLAG 0x{errors:02x})")


def write_user_row(jlink, words):
    ctrla = jlink.memory_read16(NVMCTRL_CTRLA, 1)[0]
    wait_ready(jlink)
    jlink.memory_write16(NVMCTRL_CTRLA, [ctrla & ~0x30])  # Manual write mode
    try:
        jlink.memory_write32(NVMCTRL_ADDR, [USER_ROW])
        nvm_command(jlink, CMD_EP)
        for quad in range(USER_ROW_SIZE // 16):
            nvm_command(jlink, CMD_PBC)
            jlink.memory_write32(USER_ROW + quad * 16, words[quad * 4:quad * 4 + 4])
            jlink.memory_write32(NVMCTRL_ADDR, [USER_ROW + quad * 16])
            nvm_command(jlink, CMD_WQW)
    finally:
        jlink.memory_write16(NVMCTRL_CTRLA, [ctrla])


def main():
    parser = argparse.ArgumentParser(description="Read or change the SAMD51 RAM ECCDIS fuse")
    parser.add_argument("action", nargs="?", choices=["status", "enable", "disable", "restore"], default="status")
    parser.add_argument("backup", nargs="?", help="user row backup to write back, for restore")
    args = parser.parse_args()

    jlink = pylink.JLink()
    jlink.open()
    jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jlink.set_speed(4000)
    jlink.connect(chip_name="ATSAMD51P20A")
    # Otherwise the J-Link turns writes to the user row's page buffer into a flash download with its own loader
    jlink.exec_command("DisableFlashDL")
    try:
        words = jlink.memory_read32(USER_ROW, USER_ROW_SIZE // 4)
        fuse_on = not words[ECCDIS_WORD] & ECCDIS_BIT
        active = not jlink.memory_read8(RAMECC_STATUS, 1)[0] & 1
        print(f"RAM ECC fuse: {'on' if fuse_on else 'off'} (user row word 1 = 0x{words[ECCDIS_WORD]:08x}), "
              f"active since the last reset: {'yes' if active else 'no'}")
        if args.action == "status":
            return 0

        if args.action == "restore":
            if args.backup is None:
                print("restore needs the backup file")
                return 1
            with open(args.backup, "rb") as f:
                data = f.read()
            if len(data) != USER_ROW_SIZE:
                print(f"{args.backup} is {len(data)} bytes, not a {USER_ROW_SIZE}-byte user row")
                return 1
            jlink.reset(halt=True)
            restored = list(struct.unpack(f"<{USER_ROW_SIZE // 4}I", data))
            write_user_row(jlink, restored)
            ok = jlink.memory_read32(USER_ROW, USER_ROW_SIZE // 4) == restored
            print("Restored the user row" if ok else "The user row doesn't read back as the backup!")
            jlink.reset(halt=False)
            return 0 if ok else 1

        want_on = args.action == "enable"
        if fuse_on == want_on:
            print("Nothing to change")
            return 0

        if jlink.memory_read8(DSU_STATUSB, 1)[0] & 1:
            print("The device is protected (DSU STATUSB.PROT), so the user row can't be written")
            return 1

        jlink.reset(halt=True)  # Keep the firmware off the NVM controller while the row is rewritten
        if want_on:
            for name, addr in VECTOR_TABLES.items():
                sp = jlink.memory_read32(addr, 1)[0]
                if sp > RAM_ECC_TOP:
                    print(f"{name} starts its stack at 0x{sp:08x}, in the top 128 KB RAM ECC takes. Flash a build that "
                          "only uses the bottom 128 KB (with `make flash_to_mram`) first.")
                    return 1

        backup = f"user_row_{datetime.datetime.now():%Y%m%d_%H%M%S}.bin"
        with open(backup, "wb") as f:
            f.write(struct.pack(f"<{len(words)}I", *words))
        print(f"Saved the user row to {backup}")

        new_words = list(words)
        new_words[ECCDIS_WORD] = words[ECCDIS_WORD] & ~ECCDIS_BIT if want_on else words[ECCDIS_WORD] | ECCDIS_BIT
        write_user_row(jlink, new_words)

        if jlink.memory_read32(USER_ROW, USER_ROW_SIZE // 4) != new_words:
            print(f"The user row doesn't read back as written! Restore it from {backup}")
            return 1
        print(f"User row word 1 is now 0x{new_words[ECCDIS_WORD]:08x}")

        jlink.reset(halt=False)
        active = not jlink.memory_read8(RAMECC_STATUS, 1)[0] & 1
        print(f"Reset: RAM ECC is {'on' if active else 'off'}")
        return 0 if active == want_on else 1
    finally:
        jlink.close()


if __name__ == "__main__":
    sys.exit(main())
