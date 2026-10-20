import platform
import pylink # pip install pylink-square
import os
import datetime
import subprocess

def find_rtt_block_address():
    """Address of _SEGGER_RTT in src/PVDXos.elf, or None.

    Without it, J-Link searches RAM for the "SEGGER RTT" ID and can lock onto a stale copy (the app is copied
    into RAM by the bootloader), and then reads nothing. RTT_ADDR (hex) overrides the ELF lookup.
    """
    if os.environ.get('RTT_ADDR'):
        return int(os.environ['RTT_ADDR'], 16)
    elf = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', 'src', 'PVDXos.elf'))
    try:
        out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == '_SEGGER_RTT':
            return int(parts[0], 16)
    return None

def open_rtt_channels(logfile = None):
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    addr = find_rtt_block_address()
    if addr is not None:
        print(f"[rtt_logs] RTT control block at 0x{addr:08x} (from ELF)", flush=True)
        jlink.rtt_start(addr)
    else:
        print("[rtt_logs] _SEGGER_RTT not found in src/PVDXos.elf; letting J-Link search RAM", flush=True)
        jlink.rtt_start()

    while True:
        buf1 = jlink.rtt_read(1, 2048)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1, end="", flush=True)  # RTT reads end mid-line; do not add a newline
            # Write to a log file if specified
            if logfile is not None:
                logfile.write(text1)

def main():
    # Sanity check to make sure this is not in WSL
    # if "microsoft" in platform.uname().release:
    #     print("This code cannot be executed in a WSL environment. Run natively on Windows instead!")
    #     exit(1)

    #Check if the ./logs directory exists
    current_script_dir = os.path.dirname(__file__)
    logs_path = os.path.join(current_script_dir, '..', 'logs')
    logs_path = os.path.normpath(logs_path)

    #If the logs directory does not exist, create it
    if not os.path.exists(logs_path):
        os.makedirs(logs_path)

    # Create a log file in the logs directory with the current date and time
    current_time = datetime.datetime.now()
    log_filename = current_time.strftime("%Y-%m-%d_%H-%M-%S.log")
    log_file_path = os.path.join(logs_path, log_filename)
    
    with open (log_file_path, "w") as file:
        open_rtt_channels(file)

if __name__ == "__main__":
    main()
