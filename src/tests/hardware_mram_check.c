#include "hardware_mram_check.h"

#include <string.h>
#include "atmel_start.h"
#include "hardware_mram_factory.h"
#include "logging.h"

static const uint8_t chips[] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};

static void release_all(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_set_pin_level(chips[i], true);
    }
    delay_us(1);
}

static bool baud_set(uint8_t baud) {
    spi_m_sync_disable(&SPI_MRAM);
    int32_t mode_rc = spi_m_sync_set_mode(&SPI_MRAM, SPI_MODE_0);
    int32_t baud_rc = spi_m_sync_set_baudrate(&SPI_MRAM, baud);
    spi_m_sync_enable(&SPI_MRAM);
    return mode_rc == 0 && baud_rc == 0;
}

static bool raw(const uint8_t *tx, uint8_t *rx, unsigned size) {
    struct spi_xfer xfer = {.txbuf = (uint8_t *)tx, .rxbuf = rx, .size = size};
    return spi_m_sync_transfer(&SPI_MRAM, &xfer) == (int32_t)size;
}

static bool command(unsigned chip, const uint8_t *header, unsigned header_size,
                    const uint8_t *tx, uint8_t *rx, unsigned size) {
    gpio_set_pin_level(chips[chip], false);
    bool ok = raw(header, NULL, header_size);
    if (ok && size) {
        ok = raw(tx, rx, size);
    }
    release_all();
    return ok;
}

static bool reg_read(unsigned chip, uint8_t opcode, uint8_t *value) {
    return command(chip, &opcode, 1, NULL, value, 1);
}

static bool config_read(unsigned chip, uint8_t opcode, uint8_t reg, uint8_t *value) {
    const uint8_t header[] = {opcode, 0, 0, reg};
    return command(chip, header, sizeof(header), NULL, value, 1);
}

static bool ready(unsigned chip) {
    for (unsigned i = 0; i < 50; ++i) {
        uint8_t sr = 0, fsr = 0;
        if (!reg_read(chip, 0x05, &sr) || !reg_read(chip, 0x70, &fsr)) {
            return false;
        }
        if ((fsr & 0x3A) != 0) {
            warning("mram-check: chip=%u SR=%02x FSR=%02x errors\n", chip + 1, sr, fsr);
            return false;
        }
        if ((sr & 1) == 0 && (fsr & 0x80) != 0) {
            return true;
        }
        delay_ms(1);
    }
    return false;
}

static bool write_command(unsigned chip, const uint8_t *header, unsigned header_size,
                          const uint8_t *data, unsigned size) {
    const uint8_t clear = 0x50, wren = 0x06;
    uint8_t sr = 0;
    return command(chip, &clear, 1, NULL, NULL, 0) && ready(chip) &&
           command(chip, &wren, 1, NULL, NULL, 0) && reg_read(chip, 0x05, &sr) && (sr & 2) != 0 &&
           command(chip, header, header_size, data, NULL, size) && ready(chip);
}

static void snapshot(unsigned chip) {
    const uint8_t id_command = 0x9F;
    uint8_t id[3] = {0}, sr = 0, fsr = 0, v0 = 0, v1 = 0, v8 = 0, nv8 = 0;
    bool ok = command(chip, &id_command, 1, NULL, id, 3) && reg_read(chip, 0x05, &sr) &&
              reg_read(chip, 0x70, &fsr) && config_read(chip, 0x85, 0, &v0) &&
              config_read(chip, 0x85, 1, &v1) && config_read(chip, 0x85, 8, &v8) &&
              config_read(chip, 0xB5, 8, &nv8);
    info("mram-check: chip=%u transfers=%u ID=%02x/%02x/%02x SR=%02x FSR=%02x V0=%02x V1=%02x V8=%02x NV8=%02x\n",
         chip + 1, ok, id[0], id[1], id[2], sr, fsr, v0, v1, v8, nv8);
}

static bool read_array(unsigned chip, uint32_t address, uint8_t *data, unsigned size, bool split) {
    const uint8_t header[] = {0x03, (uint8_t)(address >> 16), (uint8_t)(address >> 8), (uint8_t)address};
    if (split) {
        return command(chip, header, sizeof(header), NULL, data, size);
    }
    uint8_t tx[516] = {0}, rx[516] = {0};
    if (size > sizeof(tx) - sizeof(header)) {
        return false;
    }
    memcpy(tx, header, sizeof(header));
    gpio_set_pin_level(chips[chip], false);
    bool ok = raw(tx, rx, size + sizeof(header));
    release_all();
    memcpy(data, rx + sizeof(header), size);
    return ok;
}

static void sweep(const char *stage) {
    const uint8_t bauds[] = {119, 5, 2};
    const unsigned clocks[] = {50000, 1000000, 2000000};
    const uint32_t addresses[] = {0x980, 0x800F0, 0xFFF00};
    for (unsigned speed = 0; speed < 3; ++speed) {
        if (!baud_set(bauds[speed])) {
            warning("mram-check: clock configuration failed\n");
            return;
        }
        for (unsigned split = 0; split < 2; ++split) {
            for (unsigned chip = 0; chip < 3; ++chip) {
                unsigned matches = 0, transfers = 0;
                uint8_t first[16], next[16];
                for (unsigned addr = 0; addr < 3; ++addr) {
                    bool initial = read_array(chip, addresses[addr], first, sizeof(first), split != 0);
                    for (unsigned repeat = 0; repeat < 10; ++repeat) {
                        bool ok = initial && read_array(chip, addresses[addr], next, sizeof(next), split != 0);
                        transfers += ok;
                        matches += ok && memcmp(first, next, sizeof(first)) == 0;
                    }
                }
                info("mram-check: %s hz=%u split=%u chip=%u stable=%u/30 transfers=%u/30\n",
                     stage, clocks[speed], split, chip + 1, matches, transfers);
            }
        }
    }
}

static void reset_interfaces(void) {
    release_all();
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
    for (unsigned chip = 0; chip < 3; ++chip) {
        for (unsigned pulse = 0; pulse < 4; ++pulse) {
            gpio_set_pin_level(MRAM_MOSI, (pulse & 1) != 0);
            delay_us(1);
            gpio_set_pin_level(chips[chip], false);
            delay_us(1);
            release_all();
        }
        delay_ms(1);
    }
    SPI_MRAM_PORT_init();
    spi_m_sync_enable(&SPI_MRAM);
    delay_ms(1);
}

static bool patterns(unsigned chip) {
    uint8_t sr = 0;
    const uint8_t id_opcode = 0x9F;
    uint8_t id[3];
    if (!command(chip, &id_opcode, 1, NULL, id, 3) ||
        id[0] != 0x6B || id[1] != 0xBB || id[2] != 0x14 || !reg_read(chip, 0x05, &sr)) {
        return false;
    }
    // Audited BP mask: preserve SRWD and TB, clear BP[3:0].
    const uint8_t wrsr = 0x01, unprotected = sr & 0xA0;
    if ((sr & 0x5C) && (!write_command(chip, &wrsr, 1, &unprotected, 1) ||
                       !reg_read(chip, 0x05, &sr) || (sr & 0xFC) != unprotected)) {
        warning("mram-check: chip=%u protection clear failed SR=%02x\n", chip + 1, sr);
        return false;
    }
    const uint32_t addresses[] = {0x980, 0x800F0, 0xFFD00};
    uint8_t expected[512], actual[512];
    for (unsigned addr = 0; addr < 3; ++addr) {
        for (unsigned pattern = 0; pattern < 5; ++pattern) {
            uint32_t rng = 0xA53C9671u ^ (chip * 17u) ^ (pattern * 0x17171717u);
            for (unsigned i = 0; i < sizeof(expected); ++i) {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                expected[i] = pattern == 0 ? 0x00 : pattern == 1 ? 0xFF :
                              pattern == 2 ? 0xAA : pattern == 3 ? 0x55 : (uint8_t)rng;
            }
            uint32_t address = addresses[addr];
            const uint8_t header[] = {0x02, (uint8_t)(address >> 16), (uint8_t)(address >> 8), (uint8_t)address};
            if (!write_command(chip, header, sizeof(header), expected, sizeof(expected))) {
                warning("mram-check: chip=%u write failed addr=%06lx pattern=%u\n", chip + 1, (unsigned long)address, pattern);
                return false;
            }
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                if (!read_array(chip, address, actual, sizeof(actual), true)) {
                    return false;
                }
                for (unsigned i = 0; i < sizeof(actual); ++i) {
                    if (actual[i] != expected[i]) {
                        warning("mram-check: chip=%u mismatch addr=%06lx pattern=%u repeat=%u got=%02x expected=%02x\n",
                                chip + 1, (unsigned long)(address + i), pattern, repeat, actual[i], expected[i]);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

void hardware_mram_check(void) {
    info("mram-check: authorized destructive diagnostic; no actuators, no boot mirror writes\n");
    const uint8_t pins[] = {MRAM1_CS, MRAM2_CS, MRAM3_CS, MRAM1_RST, MRAM2_RST, MRAM3_RST};
    for (unsigned i = 0; i < sizeof(pins); ++i) {
        gpio_set_pin_level(pins[i], true);
        gpio_set_pin_function(pins[i], GPIO_PIN_FUNCTION_OFF);
        gpio_set_pin_direction(pins[i], GPIO_DIRECTION_OUT);
    }
    delay_ms(2);
    if (!baud_set(5)) {
        return;
    }
    for (unsigned chip = 0; chip < 3; ++chip) {
        snapshot(chip);
    }
    sweep("before-reset");
    reset_interfaces();
    if (!baud_set(5)) {
        return;
    }
    for (unsigned chip = 0; chip < 3; ++chip) {
        snapshot(chip);
    }
    sweep("after-reset");
#if BOARD_MRAM_FACTORY_INIT
    bool factory = hardware_mram_factory_initialize();
    info("mram-check: FACTORY=%s\n", factory ? "PASS" : "FAIL");
    if (!factory) {
        return;
    }
#endif
    if (!baud_set(2)) {
        return;
    }
    unsigned passed = 0;
    for (unsigned chip = 0; chip < 3; ++chip) {
        bool ok = patterns(chip);
        passed += ok;
        info("mram-check: chip=%u 15 x 512-byte patterns with triple readback %s\n", chip + 1, ok ? "PASS" : "FAIL");
        snapshot(chip);
    }
    info("mram-check: WRITE/READ SUMMARY=%u/3 at 2MHz\n", passed);
    sweep("after-patterns");
    release_all();
    info("mram-check: COMPLETE; idle\n");
}
