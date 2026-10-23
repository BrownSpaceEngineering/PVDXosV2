/*
 * DESTRUCTIVE, opt-in MRAM factory initialization; never a normal startup test.
 * Provenance: yi-mram-driver-audit:src/tests/mram_factory_init.c at
 * 5eb36132759c37a8b607a89a4c4b0e35b3abe2dc (verified EST3000 Rev2.1 procedure).
 * Port retains the factory sequence/readback rules, with local checked SPI/WREN
 * helpers, bare-metal CS/reset GPIO setup, and explicit mode 0 / BAUD=2 restore
 * after JESD252. Watchdog calls from the historical procedure are omitted.
 * All three arrays, OTP/control bytes and NV configuration are overwritten.
 */
#if BOARD_MRAM_FACTORY_INIT

#include "hardware_mram_factory.h"

#include <stdint.h>
#include <string.h>

#include "atmel_start.h"
#include "logging.h"

/* Keep destructive-operation diagnostics even when normal logging is disabled. */
#define FACTORY_LOG(...) SEGGER_RTT_printf(LOGGING_RTT_OUTPUT_CHANNEL, "MRAM factory: " __VA_ARGS__)
#define FACTORY_SIZE 0x100000u
#define FACTORY_BLOCK 512u

static const uint32_t factory_cs[] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};
static const uint32_t factory_rst[] = {MRAM1_RST, MRAM2_RST, MRAM3_RST};

static void factory_output_high(uint32_t pin) {
    gpio_set_pin_level(pin, true);
    gpio_set_pin_function(pin, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(pin, GPIO_DIRECTION_OUT);
}

static void factory_release(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_set_pin_level(factory_cs[i], true);
    }
}

static bool factory_spi_write(const uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {.txbuf = (uint8_t *)data, .rxbuf = NULL, .size = 0};
    /* Chunk HAL calls only; the caller owns CS for the entire transaction. */
    while (len > 0u) {
        xfer.size = len > FACTORY_BLOCK ? FACTORY_BLOCK : len;
        int32_t rc = spi_m_sync_transfer(&SPI_MRAM, &xfer);
        if (rc != (int32_t)xfer.size) {
            FACTORY_LOG("SPI write rc=%ld expected=%lu\n", (long)rc, (unsigned long)xfer.size);
            return false;
        }
        xfer.txbuf += xfer.size;
        len -= xfer.size;
    }
    return true;
}

static bool factory_spi_read(uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {.txbuf = NULL, .rxbuf = data, .size = 0};
    while (len > 0u) {
        xfer.size = len > FACTORY_BLOCK ? FACTORY_BLOCK : len;
        int32_t rc = spi_m_sync_transfer(&SPI_MRAM, &xfer);
        if (rc != (int32_t)xfer.size) {
            FACTORY_LOG("SPI read rc=%ld expected=%lu\n", (long)rc, (unsigned long)xfer.size);
            return false;
        }
        xfer.rxbuf += xfer.size;
        len -= xfer.size;
    }
    return true;
}

static bool factory_transfer(uint8_t chip, const uint8_t *header, uint32_t header_size,
                             const uint8_t *tx, uint8_t *rx, uint32_t size) {
    gpio_set_pin_level(factory_cs[chip - 1u], false);
    bool ok = factory_spi_write(header, header_size);
    if (ok && size != 0u) {
        ok = tx != NULL ? factory_spi_write(tx, size) : factory_spi_read(rx, size);
    }
    factory_release();
    return ok;
}

static bool factory_register(uint8_t chip, uint8_t opcode, uint8_t *value) {
    return factory_transfer(chip, &opcode, 1, NULL, value, 1);
}

static bool factory_write_enable(uint8_t chip) {
    const uint8_t opcode = 0x06;
    uint8_t status = 0;
    if (!factory_transfer(chip, &opcode, 1, NULL, NULL, 0) ||
        !factory_register(chip, 0x05, &status)) {
        FACTORY_LOG("chip %u WREN/status transfer failed\n", (unsigned)chip);
        return false;
    }
    if ((status & 0x02u) == 0u) {
        FACTORY_LOG("chip %u WREN latch not set SR=%02x\n", (unsigned)chip, (unsigned)status);
        return false;
    }
    return true;
}

static bool factory_ready(uint8_t chip) {
    uint8_t status = 0, flags = 0;
    for (unsigned elapsed = 0; elapsed < 50; ++elapsed) {
        if (!factory_register(chip, 0x05, &status) || !factory_register(chip, 0x70, &flags)) {
            FACTORY_LOG("chip %u ready/status transfer failed\n", (unsigned)chip);
            return false;
        }
        if ((flags & 0x3Au) != 0u) {
            FACTORY_LOG("chip %u SR=%02x FSR=%02x errors\n", (unsigned)chip, (unsigned)status, (unsigned)flags);
            return false;
        }
        if ((status & 1u) == 0u && (flags & 0x80u) != 0u) {
            return true;
        }
        delay_ms(1);
    }
    FACTORY_LOG("chip %u ready timeout SR=%02x FSR=%02x\n", (unsigned)chip, (unsigned)status, (unsigned)flags);
    return false;
}

static bool factory_write(uint8_t chip, const uint8_t *header, uint32_t header_size,
                          const uint8_t *data, uint32_t size) {
    return factory_ready(chip) && factory_write_enable(chip) &&
           factory_transfer(chip, header, header_size, data, NULL, size) && factory_ready(chip);
}

static bool factory_config_read(uint8_t chip, uint8_t opcode, uint8_t reg, uint8_t *value) {
    const uint8_t header[] = {opcode, 0, 0, reg};
    return factory_transfer(chip, header, sizeof(header), NULL, value, 1);
}

static bool factory_config(uint8_t chip, uint8_t opcode, uint8_t reg, uint8_t value, uint8_t expected) {
    const uint8_t header[] = {opcode, 0, 0, reg};
    uint8_t actual = 0;
    if (!factory_write(chip, header, sizeof(header), &value, 1) ||
        !factory_config_read(chip, opcode == 0x81 ? 0x85 : 0xB5, reg, &actual)) {
        FACTORY_LOG("chip %u config opcode=%02x reg=%02x transfer/completion failed\n",
                    (unsigned)chip, (unsigned)opcode, (unsigned)reg);
        return false;
    }
    /* EST3000 requires exact NV readback, including reserved register locations. */
    if (actual != expected) {
        FACTORY_LOG("chip %u config opcode=%02x reg=%02x got=%02x expected=%02x\n",
                    (unsigned)chip, (unsigned)opcode, (unsigned)reg, (unsigned)actual, (unsigned)expected);
        return false;
    }
    return true;
}

static bool factory_spi_restore(void) {
    spi_m_sync_disable(&SPI_MRAM);
    int32_t mode = spi_m_sync_set_mode(&SPI_MRAM, SPI_MODE_0);
    /* BAUD is a divider register value: 12 MHz / (2 * (2 + 1)) = 2 MHz. */
    int32_t baud = spi_m_sync_set_baudrate(&SPI_MRAM, 2);
    spi_m_sync_enable(&SPI_MRAM);
    if (mode != 0 || baud != 0) {
        FACTORY_LOG("SPI restore failed mode=%ld baud=%ld\n", (long)mode, (long)baud);
        return false;
    }
    return true;
}

static bool factory_reset(uint8_t chip) {
    factory_release();
    spi_m_sync_disable(&SPI_MRAM);
    gpio_set_pin_level(MRAM_SCK, false);
    gpio_set_pin_level(MRAM_MOSI, false);
    gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);
    gpio_set_pin_direction(MRAM_MOSI, GPIO_DIRECTION_OUT);
    gpio_set_pin_direction(MRAM_MISO, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(MRAM_MISO, GPIO_PULL_OFF);
    gpio_set_pin_function(MRAM_SCK, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_function(MRAM_MOSI, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_function(MRAM_MISO, GPIO_PIN_FUNCTION_OFF);
    /* JESD252: IO0 = 0,1,0,1 on successive CS pulses; no SCK edges. */
    for (unsigned pulse = 0; pulse < 4; ++pulse) {
        gpio_set_pin_level(MRAM_MOSI, (pulse & 1u) != 0u);
        delay_us(1);
        gpio_set_pin_level(factory_cs[chip - 1u], false);
        delay_us(1);
        factory_release();
        delay_us(1);
    }
    gpio_set_pin_level(MRAM_MOSI, false);
    SPI_MRAM_PORT_init();
    bool ok = factory_spi_restore();
    delay_ms(1);
    return ok;
}

static bool factory_status(uint8_t chip, uint8_t value) {
    const uint8_t opcode = 0x01;
    uint8_t actual = 0;
    if (!factory_write(chip, &opcode, 1, &value, 1) || !factory_register(chip, 0x05, &actual)) {
        return false;
    }
    if ((actual & 0xFCu) != (value & 0xFCu)) {
        FACTORY_LOG("chip %u status got=%02x expected=%02x mask=fc\n",
                    (unsigned)chip, (unsigned)actual, (unsigned)value);
        return false;
    }
    return true;
}

static bool factory_otp(uint8_t chip, uint8_t value) {
    uint8_t data[257];
    const uint8_t write_header[] = {0x42, 0, 0, 0};
    /* READOTP uses DCC, unlike register reads and ordinary READ (03). */
    const uint8_t read_header[] = {0x4B, 0, 0, 0, 0, 0};
    if (!factory_config(chip, 0x81, 8, 0xF9, 0xF9)) {
        return false;
    }
    memset(data, value, sizeof(data));
    if (!factory_write(chip, write_header, sizeof(write_header), data, sizeof(data)) ||
        !factory_transfer(chip, read_header, sizeof(read_header), NULL, data, sizeof(data))) {
        return false;
    }
    FACTORY_LOG("chip %u OTP pattern=%02x control=%02x (verify bit 0 only)\n",
                (unsigned)chip, (unsigned)value, (unsigned)data[256]);
    for (unsigned i = 0; i < sizeof(data); ++i) {
        unsigned mask = i == 256u ? 1u : 0xFFu;
        if ((data[i] & mask) != (value & mask)) {
            FACTORY_LOG("chip %u OTP address=%03x got=%02x expected=%02x mask=%02x\n",
                        (unsigned)chip, i, (unsigned)data[i], (unsigned)value, mask);
            return false;
        }
    }
    return factory_ready(chip);
}

static bool factory_array(uint8_t chip, uint8_t value) {
    uint8_t block[FACTORY_BLOCK];
    const uint8_t write_header[] = {0x02, 0, 0, 0};
    const uint8_t read_header[] = {0x03, 0, 0, 0};
    memset(block, value, sizeof(block));
    if (!factory_ready(chip) || !factory_write_enable(chip)) {
        return false;
    }
    /* One WREN and one uninterrupted CS transaction for the entire 1 MiB. */
    gpio_set_pin_level(factory_cs[chip - 1u], false);
    bool ok = factory_spi_write(write_header, sizeof(write_header));
    for (uint32_t address = 0; ok && address < FACTORY_SIZE; address += sizeof(block)) {
        ok = factory_spi_write(block, sizeof(block));
        if (!ok) {
            factory_release();
            FACTORY_LOG("chip %u array write transfer at %06x\n", (unsigned)chip, (unsigned)address);
        }
    }
    factory_release();
    if (!ok || !factory_ready(chip)) {
        return false;
    }
    FACTORY_LOG("chip %u array %02x written; verifying 1 MiB\n", (unsigned)chip, (unsigned)value);
    gpio_set_pin_level(factory_cs[chip - 1u], false);
    ok = factory_spi_write(read_header, sizeof(read_header));
    for (uint32_t address = 0; ok && address < FACTORY_SIZE; address += sizeof(block)) {
        ok = factory_spi_read(block, sizeof(block));
        if (!ok) {
            factory_release();
            FACTORY_LOG("chip %u array read transfer at %06x\n", (unsigned)chip, (unsigned)address);
            break;
        }
        for (unsigned i = 0; i < sizeof(block); ++i) {
            if (block[i] != value) {
                factory_release();
                FACTORY_LOG("chip %u array address=%06x got=%02x expected=%02x\n",
                            (unsigned)chip, (unsigned)(address + i), (unsigned)block[i], (unsigned)value);
                return false;
            }
        }
    }
    factory_release();
    return ok && factory_ready(chip);
}

bool hardware_mram_factory_initialize(void) {
    static const uint8_t array_values[] = {0x00, 0xFF, 0x00, 0xFF};
    const uint8_t clear_flags = 0x50;
    uint8_t chip;
    const char *stage = "reset";
    for (unsigned i = 0; i < 3; ++i) {
        factory_output_high(factory_cs[i]);
        factory_output_high(factory_rst[i]);
    }
    delay_ms(2);
    factory_release();
    FACTORY_LOG("DESTRUCTIVE EST3000 Rev2.1 initialization of all three 1 MiB chips\n");
    for (chip = 1; chip <= 3; ++chip) {
        stage = "JESD252 reset / ID";
        FACTORY_LOG("chip %u %s\n", (unsigned)chip, stage);
        if (!factory_reset(chip)) {
            /* No recovery commands on an unconfigured bus. */
            factory_release();
            FACTORY_LOG("chip %u FAILED stage=%s; SPI restore failed, no DFIM recovery attempted\n",
                        (unsigned)chip, stage);
            return false;
        }
        const uint8_t id_opcode = 0x9F;
        uint8_t id[3] = {0};
        if (!factory_transfer(chip, &clear_flags, 1, NULL, NULL, 0) || !factory_ready(chip) ||
            !factory_transfer(chip, &id_opcode, 1, NULL, id, sizeof(id))) {
            goto fail;
        }
        FACTORY_LOG("chip %u ID=%02x%02x%02x\n", (unsigned)chip, (unsigned)id[0], (unsigned)id[1], (unsigned)id[2]);
        if (id[0] != 0x6B || id[1] != 0xBB || id[2] != 0x14) {
            goto fail;
        }
        stage = "default SPI / three-byte / continuous configuration";
        /* JESD reset restores operation, but leaves configuration readback unchanged. */
        const uint8_t default_regs[] = {0, 5, 6, 7, 8};
        for (unsigned i = 0; i < sizeof(default_regs); ++i) {
            if (!factory_config(chip, 0x81, default_regs[i], 0xFF, 0xFF)) {
                goto fail;
            }
        }
        stage = "DFIM entry";
        FACTORY_LOG("chip %u %s\n", (unsigned)chip, stage);
        if (!factory_config(chip, 0x81, 0x1E, 0x6B, 0x01)) {
            goto fail;
        }
        for (unsigned pass = 0; pass < 2; ++pass) {
            stage = "status FF -> 00";
            FACTORY_LOG("chip %u pass %u %s\n", (unsigned)chip, pass + 1u, stage);
            if (!factory_status(chip, 0xFF) || !factory_status(chip, 0x00)) {
                goto fail;
            }
            stage = "NV registers 00..0c: 00 -> FF";
            FACTORY_LOG("chip %u pass %u %s\n", (unsigned)chip, pass + 1u, stage);
            for (unsigned pattern = 0; pattern < 2; ++pattern) {
                uint8_t value = pattern == 0u ? 0x00 : 0xFF;
                for (uint8_t reg = 0; reg <= 0x0C; ++reg) {
                    if (!factory_config(chip, 0xB1, reg, value, value)) {
                        goto fail;
                    }
                }
            }
        }
        stage = "OTP DCC=16";
        if (!factory_config(chip, 0x81, 1, 0x10, 0x10)) {
            goto fail;
        }
        for (unsigned pass = 0; pass < 2; ++pass) {
            stage = "OTP FF -> 00";
            FACTORY_LOG("chip %u pass %u %s\n", (unsigned)chip, pass + 1u, stage);
            if (!factory_otp(chip, 0xFF) || !factory_otp(chip, 0x00)) {
                goto fail;
            }
        }
        stage = "array PMM configuration";
        if (!factory_config(chip, 0x81, 8, 0xFF, 0xFF)) {
            goto fail;
        }
        /* EST3000 section 12 permits writes: avoid section 13's conflicting erase polarity. */
        for (unsigned pass = 0; pass < sizeof(array_values); ++pass) {
            stage = "full-array write / verify";
            FACTORY_LOG("chip %u array pass %u pattern=%02x\n", (unsigned)chip, pass + 1u, (unsigned)array_values[pass]);
            if (!factory_array(chip, array_values[pass])) {
                goto fail;
            }
        }
        stage = "default DCC / DFIM exit";
        if (!factory_config(chip, 0x81, 1, 0x00, 0x00) || !factory_config(chip, 0x81, 0x1E, 0x00, 0x00)) {
            goto fail;
        }
        FACTORY_LOG("chip %u complete: array FF, OTP 00, default SPI/PMM/three-byte\n", (unsigned)chip);
    }
    factory_release();
    return true;

fail:
    factory_release();
    FACTORY_LOG("chip %u FAILED stage=%s; attempting DFIM exit\n", (unsigned)chip, stage);
    /* Clear sticky errors only for recovery, never to hide an initialization failure. */
    (void)factory_transfer(chip, &clear_flags, 1, NULL, NULL, 0);
    bool exited = factory_config(chip, 0x81, 0x1E, 0x00, 0x00);
    FACTORY_LOG("chip %u recovery DFIM exit %s\n", (unsigned)chip, exited ? "verified" : "failed");
    factory_release();
    return false;
}

#endif // BOARD_MRAM_FACTORY_INIT
