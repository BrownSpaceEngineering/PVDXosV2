#!/usr/bin/env python3
"""Host regressions for the real shared MRAM driver; run with python3 this_file.py.

Only gcc and Python's standard library are required. All generated files live in
TemporaryDirectory. Internal and public API failures run in bounded subprocesses
so an unexpected fatal recovery loop fails the test rather than hanging it.
"""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

ATMEL_START = r"""
#ifndef ATMEL_START_H
#define ATMEL_START_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t hri_wdt_syncbusy_reg_t;
typedef uint8_t hri_wdt_ewctrl_reg_t;
typedef uint8_t hri_wdt_config_reg_t;
typedef uint8_t hri_wdt_clear_reg_t;
struct spi_m_sync_descriptor { int unused; };
struct spi_xfer { uint8_t *txbuf; uint8_t *rxbuf; uint32_t size; };
extern struct spi_m_sync_descriptor SPI_MRAM;
int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *, const struct spi_xfer *);
int32_t spi_m_sync_enable(struct spi_m_sync_descriptor *);
void atmel_start_init(void);
#define MRAM1_CS 1
#define MRAM2_CS 2
#define MRAM3_CS 3
#endif
"""

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mram_driver.c"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #expr); exit(1); \
} } while (0)

/* Wire opcodes/masks are literal datasheet expectations, not driver constants.
 * Commands take effect at CS rising. WRSR cannot write the WIP/WEL bits;
 * retaining WEL here also exercises callers that see it in status readback.
 */
struct device {
    uint8_t status, flags;
    int busy_polls;  /* Remaining busy RDSR responses; -1 means stuck. */
    uint8_t memory[0x20000];
};
static struct device devices[3];
static int selected, transfers, fail_at, short_transfer;
static unsigned int counts[256], delays, pets, position;
static uint8_t command, status_value, last_status_value;
static uint32_t address;
static bool deny_wel, retain_protection, stuck_busy, corrupt_writable;
static uint8_t completion_flags;
static int completion_busy;
struct payload_transfer {
    uint32_t size, address;
    unsigned int position, pets, transaction;
    int transfer;
};
static struct payload_transfer payloads[1000];
static unsigned int payload_count, transaction, payload_end_pets;
struct spi_m_sync_descriptor SPI_MRAM;

static void reset_model(uint8_t status) {
    memset(devices, 0, sizeof(devices));
    memset(counts, 0, sizeof(counts));
    for (unsigned int i = 0; i < 3; i++) {
        devices[i].status = status;
        devices[i].flags = 0x80;
    }
    selected = transfers = fail_at = short_transfer = 0;
    delays = pets = position = 0;
    command = status_value = last_status_value = 0;
    address = 0;
    deny_wel = retain_protection = stuck_busy = corrupt_writable = false;
    completion_flags = 0x80;
    completion_busy = 0;
    payload_count = transaction = payload_end_pets = 0;
}

static void commit(struct device *d) {
    d->flags = completion_flags;
    d->busy_polls = completion_busy;
}

void gpio_set_pin_level(uint32_t pin, bool high) {
    CHECK(pin >= 1 && pin <= 3);
    if (!high) {
        CHECK(selected == 0);
        selected = (int)pin;
        transaction++;
        position = address = 0;
        command = status_value = 0;
        return;
    }
    if (!selected) return;  /* Initialization can deselect an idle device. */
    CHECK(selected == (int)pin);
    struct device *d = &devices[selected - 1];
    if ((command == 0x02 || command == 0x03) && position >= 4)
        payload_end_pets = pets;
    if (position) {
        switch (command) {
        case 0x06:
            CHECK(position == 1);
            if (!deny_wel && !stuck_busy) d->status |= 0x02;
            break;
        case 0x50:
            CHECK(position == 1);
            d->flags = 0x80;
            break;
        case 0x01:
            if (position == 2 && (d->status & 0x02)) {
                last_status_value = status_value;
                uint8_t protection = retain_protection ? (d->status & 0x5c) : 0;
                d->status = (d->status & 0x03) | (status_value & 0xfc) | protection;
                if (corrupt_writable) d->status ^= 0x20;
                commit(d);
            }
            break;
        case 0x02:
            if (position > 4) {
                d->status &= (uint8_t)~0x02;
                commit(d);
            }
            break;
        default:
            break;
        }
    }
    selected = 0;
}

static void transmit(uint8_t byte) {
    struct device *d = &devices[selected - 1];
    if (position == 0) {
        command = byte;
        counts[command]++;
        CHECK(command == 0x06 || command == 0x05 || command == 0x01 ||
              command == 0x70 || command == 0x50 || command == 0x03 ||
              command == 0x02 || command == 0x9f);
    } else if (command == 0x01) {
        CHECK(position == 1);
        status_value = byte;
    } else if ((command == 0x03 || command == 0x02) && position < 4) {
        address = (address << 8) | byte;
    } else if (command == 0x02) {
        CHECK(position >= 4 && address < sizeof(d->memory));
        CHECK((d->status & 0x5f) == 0x02 && !stuck_busy && !d->busy_polls);
        d->memory[address++] = byte;
    } else {
        CHECK(false);  /* Unexpected payload or a missing CS boundary. */
    }
    position++;
}

static uint8_t receive(void) {
    struct device *d = &devices[selected - 1];
    uint8_t value = 0;
    switch (command) {
    case 0x05:
        CHECK(position == 1);
        value = d->status | ((stuck_busy || d->busy_polls) ? 0x01 : 0);
        if (d->busy_polls > 0) d->busy_polls--;
        break;
    case 0x70:
        CHECK(position == 1 && !d->busy_polls);
        value = d->flags;
        break;
    case 0x03:
        CHECK(position >= 4 && address < sizeof(d->memory));
        CHECK(!(d->status & 0x01) && !stuck_busy && !d->busy_polls);
        value = d->memory[address++];
        break;
    case 0x9f: {
        static const uint8_t id[] = {0x6b, 0xbb, 0x14};
        CHECK(position >= 1 && position <= sizeof(id));
        value = id[position - 1];
        break;
    }
    default:
        CHECK(false);
    }
    position++;
    return value;
}

int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *spi, const struct spi_xfer *xfer) {
    CHECK(spi == &SPI_MRAM && selected != 0);
    CHECK((xfer->txbuf != NULL) != (xfer->rxbuf != NULL));
    CHECK(xfer->size > 0 && xfer->size <= 512);
    CHECK(++transfers < 1000);  /* Also catches unbounded busy polling. */
    if ((command == 0x02 || command == 0x03) && position >= 4) {
        CHECK(payload_count < sizeof(payloads) / sizeof(payloads[0]));
        payloads[payload_count++] = (struct payload_transfer){
            .size = xfer->size, .address = address, .position = position,
            .pets = pets, .transaction = transaction, .transfer = transfers
        };
    }
    uint32_t accepted = xfer->size;
    if (transfers == fail_at) {
        if (!short_transfer) return -1;
        accepted--;  /* Model partial wire traffic, not just a false return. */
    }
    for (uint32_t i = 0; i < accepted; i++) {
        if (xfer->txbuf) transmit(xfer->txbuf[i]);
        else xfer->rxbuf[i] = receive();
    }
    return (int32_t)accepted;
}

int32_t spi_m_sync_enable(struct spi_m_sync_descriptor *spi) { return 0; }
void atmel_start_init(void) {}
void delay_ms(uint16_t ms) { delays += ms; CHECK(delays <= 100); }
void watchdog_pet(void) { pets++; }

static bool operation(const char *name) {
    uint8_t data[] = {0x91, 0x42, 0x73, 0xe4};
    if (!strcmp(name, "protection")) return disable_block_protection(1) == MRAM_INIT_OK;
    if (!strcmp(name, "status")) return write_status(1, 0xa0);
    if (!strcmp(name, "write")) return write_bytes(1, 0x1234, data, sizeof(data));
    if (!strcmp(name, "read")) return read_bytes(1, 0x1234, data, sizeof(data));
    if (!strcmp(name, "id")) return check_device_id(1) == MRAM_INIT_OK;
    CHECK(false);
    return false;
}

static void protection(void) {
    /* 0x56 includes WEL and the noncontiguous high BP bits. */
    static const uint8_t initial[] = {0x56, 0x04, 0x08, 0x10, 0x40, 0xfc, 0xfe};
    for (unsigned int i = 0; i < sizeof(initial); i++) {
        reset_model(initial[i]);
        CHECK(disable_block_protection(1) == MRAM_INIT_OK);
        CHECK(counts[0x01] == 1);
        CHECK(last_status_value == (initial[i] & 0xa0));
        CHECK(devices[0].status == ((initial[i] & 0xa0) | 0x02));
        CHECK(selected == 0);
    }
    static const uint8_t unprotected[] = {0x00, 0x02, 0x20, 0x80, 0xa0, 0xa2};
    for (unsigned int i = 0; i < sizeof(unprotected); i++) {
        reset_model(unprotected[i]);
        CHECK(disable_block_protection(1) == MRAM_INIT_OK);
        CHECK(counts[0x01] == 0 && counts[0x06] == 0);
        CHECK(devices[0].status == unprotected[i] && selected == 0);
    }
    reset_model(0x50);
    retain_protection = true;
    CHECK(disable_block_protection(1) == MRAM_INIT_FAIL_BLOCK_PROT);
    CHECK(counts[0x01] == 1 && selected == 0);
    reset_model(0xfc);
    corrupt_writable = true;
    CHECK(disable_block_protection(1) == MRAM_INIT_FAIL_BLOCK_PROT);
    CHECK(selected == 0);
}

static void wel_failure(void) {
    const char *names[] = {"protection", "status", "write"};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        reset_model(i == 0 ? 0x50 : 0);
        deny_wel = true;
        CHECK(!operation(names[i]));
        CHECK(counts[0x06] == 1 && counts[0x01] == 0 && counts[0x02] == 0);
        CHECK(selected == 0);
    }
}

static void busy(void) {
    const char *names[] = {"protection", "status", "write", "read"};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        reset_model(0x01);
        stuck_busy = true;
        CHECK(!operation(names[i]));
        CHECK(counts[0x05] > 0 && counts[0x05] <= 20);
        CHECK(delays > 0 && delays <= 20);
        CHECK(counts[0x06] == 0 && counts[0x01] == 0 &&
              counts[0x02] == 0 && counts[0x03] == 0);
        CHECK(selected == 0);
    }
}

static void completion(void) {
    const char *names[] = {"protection", "status", "write"};
    static const uint8_t flags[] = {0x00, 0x82, 0x88, 0x90, 0xa0, 0xba};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        for (unsigned int j = 0; j < sizeof(flags); j++) {
            reset_model(i == 0 ? 0x50 : 0);
            devices[0].flags = 0xba;  /* CLFSR must clear stale errors first. */
            completion_flags = flags[j];
            CHECK(!operation(names[i]));
            CHECK(counts[0x50] == 1 && counts[0x70] == 1);
            CHECK(counts[i == 2 ? 0x02 : 0x01] == 1 && selected == 0);
        }
    }
}

static void completion_busy_test(void) {
    const char *names[] = {"protection", "status", "write"};
    const int durations[] = {2, 19, 20, -1};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        reset_model(i == 0 ? 0x50 : 0);
        CHECK(operation(names[i]));
        unsigned int baseline = counts[0x05];
        for (unsigned int j = 0; j < sizeof(durations) / sizeof(durations[0]); j++) {
            reset_model(i == 0 ? 0x50 : 0);
            completion_busy = durations[j];
            bool succeeds = durations[j] >= 0 && durations[j] < 20;
            CHECK(operation(names[i]) == succeeds);
            CHECK(counts[i == 2 ? 0x02 : 0x01] == 1);
            CHECK(delays == (succeeds ? (unsigned int)durations[j] : 20));
            CHECK(counts[0x70] == (succeeds ? 1u : 0u));
            CHECK(counts[0x05] == (succeeds ? baseline + durations[j] : (i == 0 ? 4u : 2u) + 20));
            CHECK(selected == 0);
        }
    }
}

static void transfers_fail(void) {
    const char *names[] = {"protection", "status", "write", "read", "id"};
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        reset_model(i == 0 ? 0x56 : 0);
        CHECK(operation(names[i]));
        int total = transfers;
        for (int short_result = 0; short_result <= 1; short_result++) {
            for (int nth = 1; nth <= total; nth++) {
                reset_model(i == 0 ? 0x56 : 0);
                fail_at = nth;
                short_transfer = short_result;
                if (operation(names[i])) {
                    fprintf(stderr, "%s accepted %s transfer %d/%d\n", names[i],
                            short_result ? "short" : "negative", nth, total);
                    exit(1);
                }
                CHECK(transfers == nth && selected == 0);
            }
        }
    }
    /* Exercise the raw bool transfer wrappers independently as well. */
    for (int reading = 0; reading <= 1; reading++) {
        for (int short_result = 0; short_result <= 1; short_result++) {
            reset_model(0);
            uint8_t cmd = 0x05, value = 0;
            mram_select(1);
            if (reading) CHECK(spi_write(&cmd, 1));
            fail_at = transfers + 1;
            short_transfer = short_result;
            CHECK(!(reading ? spi_read(&value, 1) : spi_write(&cmd, 1)));
            mram_deselect(1);
            CHECK(selected == 0);
        }
    }
}

static void public_failures(bool reading) {
    uint8_t data[512], expected[512];
    memset(expected, 0xa5, sizeof(expected));
    reset_model(0);
    bool okay = reading ? read_bytes(1, 0x1200, data, 256) : write_bytes(1, 0x1200, expected, 256);
    CHECK(okay);
    int per_replica = transfers;
    /* Fail every transfer on each replica, including partial payloads. */
    for (int replica = 0; replica < 3; replica++) {
        for (int nth = 1; nth <= per_replica; nth++) {
            for (int short_result = 0; short_result <= 1; short_result++) {
                reset_model(0);
                for (int i = 0; i < 3; i++)
                    memset(devices[i].memory + 0x1200, 0x11 << i, sizeof(data));
                memcpy(data, expected, sizeof(data));
                fail_at = replica * per_replica + nth;
                short_transfer = short_result;
                if (reading) {
                    mram_read_bytes(0x1200, data, sizeof(data));
                    CHECK(transfers == fail_at);  /* No later replica/page reads. */
                    CHECK(memcmp(data, expected, sizeof(data)) == 0);  /* No vote. */
                    CHECK(counts[0x06] == 0 && counts[0x01] == 0 &&
                          counts[0x02] == 0 && counts[0x50] == 0);  /* No repair. */
                    for (int i = 0; i < 3; i++)
                        for (unsigned int byte = 0; byte < sizeof(data); byte++)
                            CHECK(devices[i].memory[0x1200 + byte] == (0x11 << i));
                } else {
                    mram_write_bytes(0x1200, data, 256);
                    CHECK(transfers == fail_at + (2 - replica) * per_replica);
                    for (int i = 0; i < 3; i++) {
                        if (i == replica) continue;
                        CHECK(memcmp(devices[i].memory + 0x1200, expected, 256) == 0);
                    }
                }
                CHECK(selected == 0);
            }
        }
    }
}

static void chunked_transfers(bool failures) {
    enum { LENGTH = 1057, BASE = 0x12345 };
    static const unsigned int sizes[] = {512, 512, 33};
    uint8_t expected[LENGTH], buffer[LENGTH + 2];
    for (unsigned int i = 0; i < LENGTH; i++)
        expected[i] = (uint8_t)((i * 37) ^ (i >> 8) ^ 0xa7);

    for (int reading = 0; reading <= 1; reading++) {
        int failure_transfers[3] = {0};
        /* First establish the real payload transfer numbers, including setup. */
        for (unsigned int scenario = 0; scenario < (failures ? 5u : 1u); scenario++) {
            reset_model(0);
            memset(devices[0].memory, 0x5a, sizeof(devices[0].memory));
            memset(buffer, 0x5a, sizeof(buffer));
            if (reading) memcpy(devices[0].memory + BASE, expected, LENGTH);
            else memcpy(buffer + 1, expected, LENGTH);
            unsigned int failed_chunk = scenario ? 1 + (scenario - 1) / 2 : 0;
            if (scenario) {
                fail_at = failure_transfers[failed_chunk];
                short_transfer = (scenario - 1) % 2;
            }
            bool okay = reading ? read_bytes(1, BASE, buffer + 1, LENGTH) :
                                  write_bytes(1, BASE, buffer + 1, LENGTH);
            CHECK(okay == (scenario == 0));
            CHECK(selected == 0 && counts[reading ? 0x03 : 0x02] == 1);
            unsigned int chunks = scenario ? failed_chunk + 1 : 3;
            CHECK(payload_count == chunks);
            unsigned int offset = 0;
            for (unsigned int i = 0; i < chunks; i++) {
                CHECK(payloads[i].size == sizes[i]);
                CHECK(payloads[i].address == BASE + offset);
                CHECK(payloads[i].position == 4 + offset);
                CHECK(payloads[i].transaction == payloads[0].transaction);
                /* Each completed HAL chunk must pet before the next transfer. */
                CHECK(payloads[i].pets == payloads[0].pets + i);
                if (!scenario) failure_transfers[i] = payloads[i].transfer;
                offset += sizes[i];
            }
            CHECK(payload_end_pets == payloads[chunks - 1].pets + (scenario ? 0u : 1u));
            unsigned int accepted = LENGTH;
            if (scenario) {
                CHECK(transfers == fail_at);  /* No next chunk or completion polling. */
                accepted = failed_chunk * 512 + (short_transfer ? sizes[failed_chunk] - 1 : 0);
            }
            CHECK(buffer[0] == 0x5a && buffer[LENGTH + 1] == 0x5a);
            CHECK(devices[0].memory[BASE - 1] == 0x5a);
            CHECK(devices[0].memory[BASE + LENGTH] == 0x5a);
            for (unsigned int i = 0; i < LENGTH; i++) {
                CHECK(buffer[i + 1] == (!reading || i < accepted ? expected[i] : 0x5a));
                CHECK(devices[0].memory[BASE + i] == (reading || i < accepted ? expected[i] : 0x5a));
            }
        }
    }
}

static void roundtrip(void) {
    reset_model(0);
    uint8_t sent[256], received[256];
    for (unsigned int i = 0; i < sizeof(sent); i++) sent[i] = (uint8_t)(i ^ 0xa7);
    devices[0].flags = 0xba;
    CHECK(write_bytes(1, 0x12345, sent, sizeof(sent)));
    CHECK(read_bytes(1, 0x12345, received, sizeof(received)));
    CHECK(memcmp(sent, received, sizeof(sent)) == 0);
    CHECK(pets >= 4 && selected == 0);
    CHECK(check_device_id(1) == MRAM_INIT_OK);
    /* Successful public void calls are safe and exercise all three CS pins. */
    mram_write_bytes(0x10123, sent, sizeof(sent));
    mram_read_bytes(0x10123, received, sizeof(received));
    CHECK(memcmp(sent, received, sizeof(sent)) == 0);
    for (unsigned int i = 0; i < 3; i++)
        CHECK(memcmp(devices[i].memory + 0x10123, sent, sizeof(sent)) == 0);
    CHECK(selected == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "protection")) protection();
    else if (!strcmp(argv[1], "wel")) wel_failure();
    else if (!strcmp(argv[1], "busy")) busy();
    else if (!strcmp(argv[1], "completion")) completion();
    else if (!strcmp(argv[1], "completion_busy")) completion_busy_test();
    else if (!strcmp(argv[1], "public_read_failure")) public_failures(true);
    else if (!strcmp(argv[1], "public_write_failure")) public_failures(false);
    else if (!strcmp(argv[1], "transfers")) transfers_fail();
    else if (!strcmp(argv[1], "roundtrip")) roundtrip();
    else if (!strcmp(argv[1], "chunks")) chunked_transfers(false);
    else if (!strcmp(argv[1], "chunk_failures")) chunked_transfers(true);
    else CHECK(false);
    return 0;
}
"""


BOOTLOADER_HARNESS = r"""
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mram_driver.h"
#include "watchdog_driver.h"
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #expr); exit(1); \
} } while (0)
static uint8_t host_ram[MRAM_OS_SIZE], host_flash[3 * MRAM_OS_SIZE];
static jmp_buf app_jump;
static bool available, valid_first;
static unsigned int reads, writes, checksums, setups, inits;
const volatile uint8_t __flash_address_zero = 0x5a;
#include "bootloader_host.c"

void watchdog_setup(void) { setups++; }
void watchdog_pet(void) {}
mram_status_t mram_init(void) {
    inits++;
    return available ? MRAM_STATE_OK : MRAM_STATE_FAILED;
}
void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size) {
    /* Development flash staging stays enabled; never dereference address 1. */
    CHECK(address == MRAM_FLASH_BASE_ADDRESS + (writes ? 1u : 0u));
    CHECK(size == (writes ? MRAM_FLASH_SIZE - 1 : 1));
    CHECK(data != NULL && writes < 2);
    writes++;
}
void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size) {
    CHECK(available && reads < 3 && data == host_ram && size == MRAM_OS_SIZE);
    CHECK(address == MRAM_OS_BASE_ADDRESS + reads * MRAM_OS_SIZE);
    memset(data, 0x6d + reads, size);
    const uint8_t checksum[] = {0x78, 0x56, 0x34, 0x12};
    memset(data + size - 4, 0, 4);
    if (valid_first && reads == 0) memcpy(data + size - 4, checksum, 4);
    reads++;
}
uint32_t crc32(const uint8_t *data, uint32_t size) {
    CHECK(data == host_ram && size == MRAM_OS_SIZE - 4);
    checksums++;
    return 0x12345678;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    CHECK(!strcmp(argv[1], "bad") || !strcmp(argv[1], "none") || !strcmp(argv[1], "valid"));
    available = strcmp(argv[1], "none") != 0;
    valid_first = !strcmp(argv[1], "valid");
    memset(host_ram, 0xee, sizeof(host_ram));
    for (unsigned int i = 0; i < MRAM_OS_SIZE; i++) {
        uint8_t expected = (uint8_t)((i * 37) ^ 0xa5);
        /* Independent single-bit errors: every replica differs, majority is expected. */
        for (unsigned int replica = 0; replica < 3; replica++)
            host_flash[replica * MRAM_OS_SIZE + i] = expected ^ (1u << replica);
    }
    if (setjmp(app_jump) == 0) {
        bootloader_main();
        CHECK(false);  /* Handoff must use go_to_app, not return. */
    }
    CHECK(setups == 1 && inits == 1);
    CHECK(reads == (valid_first ? 1u : available ? 3u : 0u));
    CHECK(checksums == reads && writes == (available ? 2u : 0u));
    for (unsigned int i = 0; i < MRAM_OS_SIZE; i++) {
        uint8_t expected = (uint8_t)((i * 37) ^ 0xa5);
        if (valid_first) {
            const uint8_t checksum[] = {0x78, 0x56, 0x34, 0x12};
            expected = i < MRAM_OS_SIZE - 4 ? 0x6d : checksum[i - (MRAM_OS_SIZE - 4)];
        }
        CHECK(host_ram[i] == expected);
    }
    return 0;
}
"""


def host_bootloader_source():
    """Adapt hardware boundaries only; keep the boot/checksum control flow real."""
    source = (ROOT / "bootloader" / "src" / "bootloader.c").read_text()
    for old, new, count in (
        ("#define FLASH_OS_BASE_ADDRESS (0x020000)",
         "#define FLASH_OS_BASE_ADDRESS (host_flash)", 1),
        ("#define RAM_OS_BASE_ADDRESS (0x20000000)",
         "#define RAM_OS_BASE_ADDRESS (host_ram)", 1),
        ("int main(void)", "int bootloader_main(void)", 2),
    ):
        if source.count(old) != count:
            raise RuntimeError(f"Bootloader host adaptation needs review: {old}")
        source = source.replace(old, new)
    source, count = re.subn(
        r"void go_to_app\(void\) \{.*\}\s*\Z",
        "void go_to_app(void) { longjmp(app_jump, 1); }\n",
        source, flags=re.DOTALL,
    )
    if count != 1:
        raise RuntimeError("Could not isolate the ARM go_to_app definition")
    return source


class MramDriverTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix="mram-driver-test-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        for name, content in {
            "atmel_start.h": ATMEL_START,
            "hal_delay.h": "#include <stdint.h>\nvoid delay_ms(uint16_t ms);\n",
            "hal_gpio.h": "#include <stdint.h>\n#include <stdbool.h>\n"
                          "void gpio_set_pin_level(uint32_t pin, bool high);\n",
            "harness.c": HARNESS,
            "bootloader_harness.c": BOOTLOADER_HARNESS,
            "bootloader_host.c": host_bootloader_source(),
        }.items():
            (directory / name).write_text(content)
        cls.executable = directory / "test_mram_driver"
        cls.bootloader_executable = directory / "test_bootloader"
        for source, executable in (("harness.c", cls.executable),
                                   ("bootloader_harness.c", cls.bootloader_executable)):
            command = [
                "gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-I", str(directory),
                "-I", str(ROOT / "bootloader" / "src"),
                str(directory / source), "-o", str(executable),
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode:
                raise RuntimeError(f"Host compilation failed ({source}):\n{result.stdout}{result.stderr}")

    def run_case(self, name, *, bootloader=False):
        executable = self.bootloader_executable if bootloader else self.executable
        result = subprocess.run(
            [str(executable), name], capture_output=True, text=True, timeout=5
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_protection_bits_and_writable_status_preservation(self):
        self.run_case("protection")

    def test_write_enable_must_latch(self):
        self.run_case("wel")

    def test_busy_is_bounded_and_read_is_rejected(self):
        self.run_case("busy")

    def test_completion_flag_errors_and_not_ready_are_rejected(self):
        self.run_case("completion")

    def test_completion_wip_waits_and_times_out_after_status_and_data_commits(self):
        self.run_case("completion_busy")

    def test_failed_public_read_returns_without_voting_or_repair(self):
        self.run_case("public_read_failure")

    def test_failed_public_write_still_attempts_healthy_replicas(self):
        self.run_case("public_write_failure")

    def test_every_negative_or_short_transfer_is_rejected_and_cs_released(self):
        self.run_case("transfers")

    def test_successful_internal_and_public_roundtrips(self):
        self.run_case("roundtrip")

    def test_large_read_and_write_keep_cs_and_address_across_hal_chunks(self):
        self.run_case("chunks")

    def test_later_hal_chunk_failures_return_false_and_release_cs(self):
        self.run_case("chunk_failures")

    def test_bootloader_exhausts_bad_mram_images_then_uses_flash(self):
        self.run_case("bad", bootloader=True)

    def test_bootloader_uses_flash_when_mram_unavailable(self):
        self.run_case("none", bootloader=True)

    def test_bootloader_valid_first_mram_image_skips_flash_fallback(self):
        self.run_case("valid", bootloader=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
