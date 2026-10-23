#if defined(UNITTEST) && defined(TEST_MRAM) && defined(MRAM_FACTORY_INIT)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver_init.h"
#include "drivers/mram/mram_driver.h"
#include "drivers/watchdog/watchdog_driver.h"
#include "logging.h"

extern bool spi_write(const uint8_t *data, uint32_t len);
extern bool spi_read(uint8_t *data, uint32_t len);
extern bool write_enable(uint8_t mram);

/* test_log is compiled out in UNITTEST; retain destructive-operation diagnostics. */
#define FACTORY_LOG(...) SEGGER_RTT_printf(LOGGING_RTT_OUTPUT_CHANNEL, "MRAM factory: " __VA_ARGS__)
#define FACTORY_SIZE 0x100000u
#define FACTORY_BLOCK 512u

static const uint32_t factory_cs[] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};

static void factory_release(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_set_pin_level(factory_cs[i], true);
    }
}

static bool factory_transfer(uint8_t chip, const uint8_t *header, uint32_t header_size,
                             const uint8_t *tx, uint8_t *rx, uint32_t size) {
    gpio_set_pin_level(factory_cs[chip - 1u], false);
    bool ok = spi_write(header, header_size);
    if (ok && size != 0u) {
        ok = tx != NULL ? spi_write(tx, size) : spi_read(rx, size);
    }
    factory_release();
    return ok;
}

static bool factory_register(uint8_t chip, uint8_t opcode, uint8_t *value) {
    return factory_transfer(chip, &opcode, 1, NULL, value, 1);
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
        watchdog_pet();
        delay_ms(1);
    }
    FACTORY_LOG("chip %u ready timeout SR=%02x FSR=%02x\n", (unsigned)chip, (unsigned)status, (unsigned)flags);
    return false;
}

static bool factory_write(uint8_t chip, const uint8_t *header, uint32_t header_size,
                          const uint8_t *data, uint32_t size) {
    return factory_ready(chip) && write_enable(chip) &&
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

static void factory_reset(uint8_t chip) {
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
    spi_m_sync_enable(&SPI_MRAM);
    delay_ms(1);
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
    if (!factory_ready(chip) || !write_enable(chip)) {
        return false;
    }
    /* One WREN and one uninterrupted CS transaction for the entire 1 MiB. */
    gpio_set_pin_level(factory_cs[chip - 1u], false);
    bool ok = spi_write(write_header, sizeof(write_header));
    for (uint32_t address = 0; ok && address < FACTORY_SIZE; address += sizeof(block)) {
        ok = spi_write(block, sizeof(block));
        watchdog_pet();
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
    ok = spi_write(read_header, sizeof(read_header));
    for (uint32_t address = 0; ok && address < FACTORY_SIZE; address += sizeof(block)) {
        ok = spi_read(block, sizeof(block));
        watchdog_pet();
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

bool mram_factory_initialize_all(void) {
    static const uint8_t array_values[] = {0x00, 0xFF, 0x00, 0xFF};
    const uint8_t clear_flags = 0x50;
    uint8_t chip;
    const char *stage = "reset";
    factory_release();
    FACTORY_LOG("DESTRUCTIVE EST3000 Rev2.1 initialization of all three 1 MiB chips\n");
    for (chip = 1; chip <= 3; ++chip) {
        stage = "JESD252 reset / ID";
        FACTORY_LOG("chip %u %s\n", (unsigned)chip, stage);
        factory_reset(chip);
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

#endif
