/**
 * Minimal SCH16T-K01 bench diagnostic, using the SCH1600 reference sequence
 * and SCH16T datasheet sections 5.2, 6 and 7. No scheduler or vendor HAL needed.
 */
#include "hardware_gyro_check.h"

#include <stdint.h>

#include "atmel_start.h"
#include "logging.h"

#define GYRO_FRAME_MASK UINT64_C(0xFFFFFFFFFFFF)
#define GYRO_DATA_BIT   UINT64_C(0x800000000000)
#define GYRO_SA_MASK    UINT64_C(0x7FE000000000)
#define GYRO_ERROR_MASK UINT64_C(0x001E00000000)
#define GYRO_FRAME_TYPE UINT64_C(0x000800000000)
#define GYRO_WRITE_BIT  UINT64_C(0x002000000000)
#define GYRO_COMP_ID    0x0023u

#define GYRO_REG_RATE_X 0x01u
#define GYRO_REG_TEMP   0x10u
#define GYRO_REG_SUM    0x14u
#define GYRO_REG_FILT_RATE  0x25u
#define GYRO_REG_FILT_ACC12 0x26u
#define GYRO_REG_FILT_ACC3  0x27u
#define GYRO_REG_RATE_CTRL  0x28u
#define GYRO_REG_ACC12_CTRL 0x29u
#define GYRO_REG_ACC3_CTRL  0x2Au
#define GYRO_REG_MODE       0x35u
#define GYRO_REG_RESET      0x36u
#define GYRO_REG_ID         0x3Cu

static bool gyro_ready;
static uint8_t gyro_target;

/* SCH16T section 6.5: shift bits 47..0 with the CRC byte zeroed, seed FF.
 * Polynomial x^8+x^5+x^3+x^2+x+1 (0x2F in this shift implementation).
 */
static uint8_t gyro_crc8(uint64_t frame) {
    uint64_t data = frame & UINT64_C(0xFFFFFFFFFF00);
    uint8_t crc = 0xFF;

    for (int bit = 47; bit >= 0; bit--) {
        uint8_t input = (uint8_t)((data >> bit) & 1u);
        bool top = (crc & 0x80u) != 0;
        crc = (uint8_t)(crc << 1);
        if (top) {
            crc ^= 0x2Fu;
        }
        crc ^= input;
    }
    return crc;
}

static uint64_t gyro_request(uint8_t address, bool write, uint32_t data) {
    uint64_t frame = ((uint64_t)gyro_target << 46) | ((uint64_t)address << 38) | GYRO_FRAME_TYPE;
    if (write) {
        frame |= GYRO_WRITE_BIT;
    }
    frame |= (uint64_t)(data & 0xFFFFFu) << 8;
    return frame | gyro_crc8(frame);
}

static bool gyro_transfer(uint64_t request, uint64_t *response) {
    uint8_t tx[6];
    uint8_t rx[6] = {0};
    for (unsigned i = 0; i < sizeof(tx); i++) {
        tx[i] = (uint8_t)(request >> (8u * (5u - i)));
    }
    struct spi_xfer transfer = {.txbuf = tx, .rxbuf = rx, .size = sizeof(tx)};

    gpio_set_pin_level(GYRO_CS, false);
    int32_t rc = spi_m_sync_transfer(&SPI_MAGNETOMETER_GYRO, &transfer);
    gpio_set_pin_level(GYRO_CS, true);
    /* Also exceeds the datasheet's CS-high / inter-frame timing minimum. */
    delay_us(1);

    if (rc != (int32_t)sizeof(tx)) {
        warning("gyro-check: SPI transfer rc=%ld expected=6\n", (long)rc);
        return false;
    }
    *response = 0;
    for (unsigned i = 0; i < sizeof(rx); i++) {
        *response = (*response << 8) | rx[i];
    }
    return true;
}

static bool gyro_validate(uint64_t response, uint8_t address, bool sensor_data) {
    uint8_t received_crc = (uint8_t)response;
    uint8_t expected_crc = gyro_crc8(response);
    uint16_t source = (uint16_t)((response & GYRO_SA_MASK) >> 37);
    bool data_bit = (response & GYRO_DATA_BIT) != 0;

    if (response == 0 || response == GYRO_FRAME_MASK || received_crc != expected_crc ||
        source != ((uint16_t)gyro_target << 8 | address) || data_bit != sensor_data) {
        warning("gyro-check: reg=0x%02x invalid response=%04x%08lx CRC=%02x expected=%02x SA=%03x D=%u\n",
                (unsigned)address, (unsigned)(response >> 32), (unsigned long)(uint32_t)response,
                (unsigned)received_crc, (unsigned)expected_crc, (unsigned)source, (unsigned)data_bit);
        return false;
    }
    return true;
}

static uint32_t gyro_info(uint64_t response) {
    return (uint32_t)((response >> 8) & 0xFFFFFu);
}

static unsigned gyro_errors(uint64_t response) {
    /* Raw S/CE/IDS group from the reference ERROR_FIELD_MASK; zero is healthy. */
    return (unsigned)((response & GYRO_ERROR_MASK) >> 33);
}

static bool gyro_read(uint8_t address, bool sensor_data, bool require_normal, uint64_t *response) {
    uint64_t request = gyro_request(address, false, 0);
    uint64_t discarded;

    /* Out-of-frame protocol: the first reply belongs to an earlier request.
     * Repeating a read makes this independent of the previous bus transaction.
     */
    if (!gyro_transfer(request, &discarded) || !gyro_transfer(request, response) ||
        !gyro_validate(*response, address, sensor_data)) {
        return false;
    }
    if (require_normal && gyro_errors(*response) != 0) {
        warning("gyro-check: reg=0x%02x CRC=OK status-flags=0x%x (expected 0)\n",
                (unsigned)address, gyro_errors(*response));
        return false;
    }
    return true;
}

static bool gyro_write_verify(uint8_t address, uint32_t value) {
    uint64_t discarded;
    uint64_t response;
    if (!gyro_transfer(gyro_request(address, true, value), &discarded) ||
        !gyro_read(address, false, false, &response)) {
        return false;
    }
    if (gyro_info(response) != value) {
        warning("gyro-check: reg=0x%02x readback=0x%05lx expected=0x%05lx\n",
                (unsigned)address, (unsigned long)gyro_info(response), (unsigned long)value);
        return false;
    }
    return true;
}

/* Reads also clear latched diagnostic flags. Startup requires a pass before EOI
 * and two passes afterwards, as in the reference SCH1_init().
 */
static bool gyro_status(bool *healthy) {
    uint16_t values[10];
    unsigned flags = 0;
    *healthy = true;
    for (unsigned i = 0; i < 10; i++) {
        uint64_t response;
        if (!gyro_read((uint8_t)(GYRO_REG_SUM + i), false, false, &response)) {
            return false;
        }
        values[i] = (uint16_t)gyro_info(response);
        flags |= gyro_errors(response);
        if (values[i] != 0xFFFFu) {
            *healthy = false;
        }
    }
    if (flags != 0) {
        *healthy = false;
    }
    info("gyro-check: status CRC=OK flags=0x%x SUM=%04x SAT=%04x COM=%04x RATE_COM=%04x\n",
         flags, (unsigned)values[0], (unsigned)values[1], (unsigned)values[2], (unsigned)values[3]);
    info("gyro-check: status RATE_X/Y/Z=%04x/%04x/%04x ACC_X/Y/Z=%04x/%04x/%04x healthy=%u\n",
         (unsigned)values[4], (unsigned)values[5], (unsigned)values[6],
         (unsigned)values[7], (unsigned)values[8], (unsigned)values[9], (unsigned)*healthy);
    return true;
}

bool hardware_gyro_init(void) {
    uint64_t response;
    bool healthy;
    gyro_ready = false;
    gpio_set_pin_level(GYRO_CS, true);
    delay_ms(32);

    bool found = false;
    for (gyro_target = 0; gyro_target < 4; gyro_target++) {
        info("gyro-check: read-only COMP_ID probe TA=%u\n", gyro_target);
        if (gyro_read(GYRO_REG_ID, false, false, &response) && gyro_info(response) == GYRO_COMP_ID) {
            found = true;
            break;
        }
    }
    if (!found) {
        warning("gyro-check: no CRC-valid SCH16T identity on TA=0..3; no configuration written\n");
        return false;
    }
    info("gyro-check: TA=%u COMP_ID=0x%04x expected=0x0023 CRC=OK status-flags=0x%x\n",
         gyro_target, (unsigned)gyro_info(response), gyro_errors(response));

    /* CTRL_RESET.SOFTRESET_CTRL=1: reference REQ_SOFTRESET=0x0DA800000AC3.
     * The returned frame predates reset and is deliberately discarded.
     */
    if (!gyro_transfer(gyro_request(GYRO_REG_RESET, true, 0xAu), &response)) {
        return false;
    }
    delay_ms(32);
    if (!gyro_read(GYRO_REG_MODE, false, false, &response) || gyro_info(response) != 0) {
        warning("gyro-check: software reset not confirmed (CTRL_MODE must be zero)\n");
        return false;
    }

    /* Reference settings: 30 Hz filters, Rate1/2=1600 LSB/(deg/s),
     * Acc1/2/3=3200 LSB/(m/s^2), XYZ channel-2 decimation=32 (code 4).
     * DRY/SYNC and all self-test controls retain software-reset defaults.
     */
    const uint32_t rate_control = (2u << 12) | (2u << 9) | (4u << 6) | (4u << 3) | 4u;
    const uint32_t acc_control = (1u << 12) | (1u << 9) | (4u << 6) | (4u << 3) | 4u;
    if (!gyro_write_verify(GYRO_REG_FILT_RATE, 0x49u) ||
        !gyro_write_verify(GYRO_REG_FILT_ACC12, 0x49u) ||
        !gyro_write_verify(GYRO_REG_FILT_ACC3, 0x49u) ||
        !gyro_write_verify(GYRO_REG_RATE_CTRL, rate_control) ||
        !gyro_write_verify(GYRO_REG_ACC12_CTRL, acc_control) ||
        !gyro_write_verify(GYRO_REG_ACC3_CTRL, 1u) ||
        !gyro_write_verify(GYRO_REG_MODE, 1u)) {
        return false;
    }

    delay_ms(215);
    if (!gyro_status(&healthy) || !gyro_write_verify(GYRO_REG_MODE, 3u)) {
        return false;
    }
    delay_ms(3);
    if (!gyro_status(&healthy) || !gyro_status(&healthy) || !healthy) {
        warning("gyro-check: sensing startup/status failed; live samples disabled\n");
        return false;
    }

    gyro_ready = true;
    info("gyro-check: volatile sensing init OK; EOI set, polling without DRDY/freshness check\n");
    return true;
}

static int32_t gyro_signed20(uint64_t response) {
    uint32_t value = gyro_info(response);
    return (value & 0x80000u) ? (int32_t)value - 0x100000 : (int32_t)value;
}

bool hardware_gyro_sample(void) {
    int32_t raw[6];
    uint64_t response;
    bool healthy;
    if (!gyro_ready) {
        warning("gyro-check: sample skipped; successful init required\n");
        return false;
    }
    for (unsigned i = 0; i < 6; i++) {
        if (!gyro_read((uint8_t)(GYRO_REG_RATE_X + i), true, true, &response)) {
            return false;
        }
        raw[i] = gyro_signed20(response);
    }
    if (!gyro_read(GYRO_REG_TEMP, true, true, &response)) {
        return false;
    }
    /* Temperature is signed 16-bit data, left-aligned in the 20-bit field. */
    int32_t temperature_mc = (gyro_signed20(response) / 16) * 10;
    if (!gyro_status(&healthy) || !healthy) {
        warning("gyro-check: live sample rejected; unhealthy status\n");
        return false;
    }

    info("gyro-check: live CRC/address/D/status OK rate_mdps=%ld/%ld/%ld accel_mm_s2=%ld/%ld/%ld temp_mC=%ld\n",
         (long)(raw[0] * 1000 / 1600), (long)(raw[1] * 1000 / 1600), (long)(raw[2] * 1000 / 1600),
         (long)(raw[3] * 1000 / 3200), (long)(raw[4] * 1000 / 3200), (long)(raw[5] * 1000 / 3200),
         (long)temperature_mc);
    return true;
}
