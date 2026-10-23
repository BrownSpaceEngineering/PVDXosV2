#include "hardware_smoke_check.h"

#include <string.h>

#include "atmel_start.h"
#include "fuel_gauge_driver.h"
#include "hardware_gyro_check.h"
#include "logging.h"

static void output_high(uint8_t pin) {
    gpio_set_pin_level(pin, true);
    gpio_set_pin_function(pin, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(pin, GPIO_DIRECTION_OUT);
}

static bool configure_spi(struct spi_m_sync_descriptor *spi) {
    spi_m_sync_disable(spi);
    // Generated SERCOM0/4 core clocks are 12 MHz: BAUD=5 gives 1 MHz.
    int32_t mode = spi_m_sync_set_mode(spi, SPI_MODE_0);
    int32_t baud = spi_m_sync_set_baudrate(spi, 5);
    spi_m_sync_enable(spi);
    if (mode != 0 || baud != 0) {
        warning("board-check: SPI setup failed mode=%ld baud=%ld\n", (long)mode, (long)baud);
        return false;
    }
    return true;
}

static bool transfer(struct spi_m_sync_descriptor *spi, uint8_t cs, uint8_t *tx, uint8_t *rx, unsigned size) {
    struct spi_xfer xfer = {.txbuf = tx, .rxbuf = rx, .size = size};
    gpio_set_pin_level(cs, false);
    int32_t rc = spi_m_sync_transfer(spi, &xfer);
    gpio_set_pin_level(cs, true);
    delay_us(1);
    if (rc != (int32_t)size) {
        warning("board-check: SPI transfer CS=%u rc=%ld expected=%u\n", cs, (long)rc, size);
        return false;
    }
    return true;
}

static bool mram_check(unsigned number, uint8_t cs) {
    uint8_t tx[20] = {0x9F}, rx[20] = {0};
    if (!transfer(&SPI_MRAM, cs, tx, rx, 4)) {
        return false;
    }
    bool id_ok = rx[1] == 0x6B && rx[2] == 0xBB && rx[3] == 0x14;
    info("board-check: MRAM%u RDID=%02x %02x %02x expected=6b bb 14 %s\n",
         number, rx[1], rx[2], rx[3], id_ok ? "OK" : "FAIL");
    memset(tx, 0, sizeof(tx));
    tx[0] = 0x05;
    if (!transfer(&SPI_MRAM, cs, tx, rx, 2)) {
        return false;
    }
    uint8_t status = rx[1];
    info("board-check: MRAM%u status=0x%02x WIP=%u WEL=%u (not modified)\n",
         number, status, status & 1u, (status >> 1) & 1u);

    // Read the same small range twice. No WREN, array writes or protection changes.
    uint8_t first[16];
    memset(tx, 0, sizeof(tx));
    tx[0] = 0x03;
    tx[2] = 0x09;
    tx[3] = 0x80;
    if (!transfer(&SPI_MRAM, cs, tx, rx, sizeof(tx))) {
        return false;
    }
    memcpy(first, &rx[4], sizeof(first));
    if (!transfer(&SPI_MRAM, cs, tx, rx, sizeof(tx))) {
        return false;
    }
    bool stable = memcmp(first, &rx[4], sizeof(first)) == 0;
    if (!stable) {
        for (unsigned i = 0; i < sizeof(first); i++) {
            if (first[i] != rx[4 + i]) {
                warning("board-check: MRAM%u unstable read offset=%u first=%02x second=%02x xor=%02x\n",
                        number, i, first[i], rx[4 + i], first[i] ^ rx[4 + i]);
                break;
            }
        }
    }
    info("board-check: MRAM%u read-only addr=0x000980 first=%02x %02x %02x %02x repeated-read=%s\n",
         number, first[0], first[1], first[2], first[3], stable ? "OK" : "FAIL");
    return id_ok && stable && (status & 1u) == 0;
}

static bool mag_read(uint8_t reg, uint8_t *data, unsigned size) {
    uint8_t tx[16] = {0}, rx[16] = {0};
    if (size > sizeof(tx) - 1) {
        return false;
    }
    tx[0] = reg | 0x80;
    if (!transfer(&SPI_MAGNETOMETER_GYRO, MAGNETOMETER_CS, tx, rx, size + 1)) {
        return false;
    }
    memcpy(data, &rx[1], size);
    return true;
}

static bool mag_write(uint8_t reg, const uint8_t *data, unsigned size) {
    uint8_t tx[16] = {0}, rx[16] = {0};
    if (size > sizeof(tx) - 1) {
        return false;
    }
    tx[0] = reg & 0x7F;
    memcpy(&tx[1], data, size);
    return transfer(&SPI_MAGNETOMETER_GYRO, MAGNETOMETER_CS, tx, rx, size + 1);
}

static bool mag_config_check(void) {
    const uint8_t expected_cc[6] = {0, 200, 0, 200, 0, 200};
    uint8_t cc[6], tmrc = 0;
    // Reading CMM terminates continuous measurement; check only nondestructive registers here.
    bool read_ok = mag_read(0x04, cc, sizeof(cc)) && mag_read(0x0B, &tmrc, 1);
    bool ok = read_ok && memcmp(cc, expected_cc, sizeof(cc)) == 0 && tmrc == 0x9A;
    info("board-check: RM3100 volatile configuration readback=%s TMRC=0x%02x\n", ok ? "OK" : "FAIL", tmrc);
    return ok;
}

static bool mag_init(void) {
    uint8_t id = 0;
    if (!mag_read(0x36, &id, 1)) {
        return false;
    }
    info("board-check: RM3100 REVID=0x%02x expected=0x22\n", id);
    if (id != 0x22) {
        return false;
    }
    const uint8_t cc[6] = {0, 200, 0, 200, 0, 200};
    const uint8_t tmrc = 0x9A, cmm = 0x79;
    return mag_write(0x04, cc, sizeof(cc)) && mag_write(0x0B, &tmrc, 1) && mag_config_check() && mag_write(0x01, &cmm, 1);
}

static bool mag_sample(void) {
    uint8_t id = 0, status = 0, bytes[9];
    if (!mag_read(0x36, &id, 1) || id != 0x22 || !mag_config_check() || !mag_read(0x34, &status, 1)) {
        return false;
    }
    if ((status & 0x80) == 0) {
        warning("board-check: RM3100 no fresh measurement (status=0x%02x)\n", status);
        return false;
    }
    if (!mag_read(0x24, bytes, sizeof(bytes))) {
        return false;
    }
    int32_t xyz[3];
    for (unsigned i = 0; i < 3; i++) {
        uint32_t value = ((uint32_t)bytes[3 * i] << 16) | ((uint32_t)bytes[3 * i + 1] << 8) | bytes[3 * i + 2];
        xyz[i] = (value & 0x800000u) ? (int32_t)value - 0x1000000 : (int32_t)value;
    }
    info("board-check: RM3100 REVID=22 DRDY=1 live XYZ counts=%ld/%ld/%ld\n", (long)xyz[0], (long)xyz[1], (long)xyz[2]);
    return true;
}

void hardware_smoke_check(void) {
    info("board-check: reworked PCB; MRAM1/2/3, RM3100, SCH16T, TCA9546A\n");
    info("board-check: normal SERCOM6 SDA=PD09 SCL=PD08; no SERCOM7 workaround, gauge reads or H-bridge exercise\n");
    info("board-check: MRAM READ-ONLY; sensor volatile controls may change; no memory integrity/retention write test\n");
    output_high(MRAM1_CS);
    output_high(MRAM2_CS);
    output_high(MRAM3_CS);
    output_high(MRAM1_RST);
    output_high(MRAM2_RST);
    output_high(MRAM3_RST);
    output_high(MAGNETOMETER_CS);
    output_high(GYRO_CS);
    delay_ms(2);
    bool mram_bus = configure_spi(&SPI_MRAM);
    bool sensor_bus = configure_spi(&SPI_MAGNETOMETER_GYRO);
    bool mag_ok = sensor_bus && mag_init();
    bool gyro_ok = sensor_bus && hardware_gyro_init();
    unsigned pass[6] = {0};
    const uint8_t mram_cs[3] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};
    for (unsigned cycle = 1; cycle <= 5; cycle++) {
        delay_ms(1000);
        for (unsigned chip = 0; chip < 3; chip++) {
            bool ok = mram_bus && mram_check(chip + 1, mram_cs[chip]);
            pass[chip] += ok;
            info("board-check: cycle=%u MRAM%u %s\n", cycle, chip + 1, ok ? "PASS" : "FAIL");
        }
        bool mag = mag_ok && mag_sample();
        bool gyro = gyro_ok && hardware_gyro_sample();
        bool mux = fg_mux_self_test() == SUCCESS;
        pass[3] += mag;
        pass[4] += gyro;
        pass[5] += mux;
        info("board-check: cycle=%u RM3100=%s GYRO=%s MUX=%s\n", cycle,
             mag ? "PASS" : "FAIL", gyro ? "PASS" : "FAIL", mux ? "PASS" : "FAIL");
    }
    info("board-check: SUMMARY /5 MRAM1=%u MRAM2=%u MRAM3=%u RM3100=%u GYRO=%u MUX=%u\n",
         pass[0], pass[1], pass[2], pass[3], pass[4], pass[5]);
    info("board-check: complete; idle, no actuators exercised\n");
}
