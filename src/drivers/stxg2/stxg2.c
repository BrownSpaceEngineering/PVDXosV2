/**
 * stxg2.c
 *
 * STXG2 S-band transmitter driver. See stxg2.h.
 * Refs: ICD-01-10022-A6 (ICD), USM-01-10022-A8 (USM).
 */

#include "stxg2.h"

#include <string.h>

#include "hal_gpio.h"
#include "hal_i2c_m_sync.h"
#include "hal_spi_m_sync.h"
#include "logging.h"

// NOTE: these are all estimates
#define STXG2_POLL_MS 10u
#define STXG2_POWER_SETTLE_MS 100u // From EN high to RST high
#define STXG2_BOOT_TIMEOUT_MS 5000u
#define STXG2_MODE_TIMEOUT_MS 2000u
#define STXG2_MEM_TIMEOUT_MS 2000u
#define STXG2_SPI_WAIT_LIMIT 1000u // 1 ms polls with no buffer room
#define STXG2_SCRATCH_PATTERN 0xB5E0CAFEu

static uint8_t stxg2_spi_tx_buffer[STXG2_SPI_CHUNK_BYTES] = {0x00};
static struct spi_xfer stxg2_xfer = {.rxbuf = NULL, .txbuf = stxg2_spi_tx_buffer, .size = 0};

static int32_t last_i2c_error = 0;          // ASF return code of the last failed transfer
static bool i2c_status_needs_clear = false; // A failed transfer can set flags in I2C_STAT

static bool store_active = false;
static uint32_t store_words_sent = 0; // Since SPI enable
static uint8_t store_carry[4] = {0};  // Partial word
static size_t store_carry_len = 0;

/**
 * \brief Blocks for ms
 */
static void stxg2_delay_ms(uint32_t ms) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        delay_ms((uint16_t)ms);
    } else {
        TickType_t ticks = pdMS_TO_TICKS(ms);
        vTaskDelay(ticks ? ticks : 1);
    }
}

/**
 * \brief Polls addr until (value & mask) == expected
 *
 * A failed read counts as not ready. The radio does not answer I2C while it changes mode.
 *
 * \param last_value Last value read or NULL.
 * \returns ERROR_NOT_READY on timeout, or the read error if all reads failed
 */
static status_t stxg2_poll_reg(uint16_t addr, uint32_t mask, uint32_t expected, uint32_t timeout_ms, uint32_t *last_value) {
    uint32_t value = 0;
    status_t read_result = ERROR_NOT_READY;
    bool read_succeeded = false;
    for (uint32_t waited = 0; waited <= timeout_ms; waited += STXG2_POLL_MS) {
        read_result = stxg2_read_reg(addr, &value);
        if (read_result == SUCCESS) {
            read_succeeded = true;
            if (last_value != NULL) {
                *last_value = value;
            }
            if ((value & mask) == expected) {
                return SUCCESS;
            }
        }
        stxg2_delay_ms(STXG2_POLL_MS);
    }
    if (!read_succeeded) {
        warning("stxg2: all reads of 0x%04x failed (i2c error %ld)\n", addr, (long)last_i2c_error);
        return read_result;
    }
    return ERROR_NOT_READY;
}

/**
 * \brief Makes sure that index < MEMORY_SIZE (USM 9.2.7.2)
 */
static status_t stxg2_check_index(uint32_t index) {
    uint32_t mem_size = 0;
    ret_err_status(stxg2_read_reg(REG_MEMORY_SIZE, &mem_size), "stxg2: memory size read failed\n");
    if (index >= mem_size) {
        warning("stxg2: index %lu is outside memory size %lu\n", (unsigned long)index, (unsigned long)mem_size);
        return ERROR_BAD_ARGS;
    }
    return SUCCESS;
}

/**
 * \brief Waits for memory state Idle
 */
static status_t stxg2_wait_memory_idle(void) {
    uint32_t mem_status = 0;
    status_t result = stxg2_poll_reg(REG_MEMORY_STATUS, 0xFFu, MEMORY_STATE_IDLE, STXG2_MEM_TIMEOUT_MS, &mem_status);
    if (result == ERROR_NOT_READY) {
        warning("stxg2: memory not idle, state %lu\n", (unsigned long)MEMORY_STATUS_STATE(mem_status));
    }
    return result;
}

/**
 * \brief Sends one I2C message. On failure, sends a STOP to release the bus.
 *
 * \returns ERROR_I2C_FAILED on failure. last_i2c_error keeps the ASF code (-2 = NACK, -5 = bus error).
 */
static status_t stxg2_i2c_transfer(struct _i2c_m_msg *msg) {
    int32_t rc = _i2c_m_sync_transfer(&I2C_SBAND.device, msg);
    if (rc != 0) {
        last_i2c_error = rc;
        i2c_status_needs_clear = true;
        i2c_m_sync_send_stop(&I2C_SBAND);
        return ERROR_I2C_FAILED;
    }
    return SUCCESS;
}

/**
 * \brief Reads I2C_STAT to clear its flags without checking them
 */
static status_t stxg2_clear_i2c_status(void) {
    uint32_t ignored = 0;
    status_t result = stxg2_read_reg(REG_I2C_STATUS, &ignored);
    if (result == SUCCESS) {
        i2c_status_needs_clear = false;
    }
    return result;
}

/**
 * \brief Reads and clears I2C_STAT (USM 9.2.2.1)
 *
 * \returns ERROR_WRITE_FAILED if a flag is set
 */
status_t stxg2_check_i2c_status(void) {
    uint32_t i2c_status = 0;
    ret_err_status(stxg2_read_reg(REG_I2C_STATUS, &i2c_status), "stxg2: I2C status read failed\n");
    i2c_status_needs_clear = false;
    if (i2c_status & I2C_STATUS_MSK) {
        warning("stxg2: I2C status flags 0x%02lx (invalid addr|bad size|read-only|internal|overflow)\n",
                (unsigned long)(i2c_status & I2C_STATUS_MSK));
        return ERROR_WRITE_FAILED;
    }
    return SUCCESS;
}

/**
 * \brief Writes a register.
 *
 * Frame: addr, then value, LSB first (ICD 3.1.1).
 *
 * \note I2C_STAT read after write_reg() ensures successful write
*/
status_t stxg2_write_reg(uint16_t addr, uint32_t value) {
    if (i2c_status_needs_clear) {
        stxg2_clear_i2c_status();
    }

    uint8_t buf[6];
    buf[0] = (uint8_t)(addr & 0xFF);
    buf[1] = (uint8_t)((addr >> 8) & 0xFF);
    buf[2] = (uint8_t)(value & 0xFF);
    buf[3] = (uint8_t)((value >> 8) & 0xFF);
    buf[4] = (uint8_t)((value >> 16) & 0xFF);
    buf[5] = (uint8_t)((value >> 24) & 0xFF);

    struct _i2c_m_msg msg = {
        .addr = STXG2_I2C_ADDR,
        .len = sizeof(buf),
        .buffer = buf,
        .flags = I2C_M_SEVEN | I2C_M_STOP, // No I2C_M_RD = write
    };
    if (stxg2_i2c_transfer(&msg) != SUCCESS) {
        warning("stxg2: write of 0x%04x failed (i2c error %ld)\n", addr, (long)last_i2c_error);
        return ERROR_I2C_FAILED;
    }
    return stxg2_check_i2c_status();
}

/**
 * \brief Reads a 4-byte register value, LSB first.
 *
 * \note ICD 3.1.2 shows a 2-byte address echo before the value. The radio does not send it: a 6-byte read of
 * REG_ID returned 32 58 54 53 00 00.
 */
status_t stxg2_read_reg(uint16_t addr, uint32_t *value) {
    if (value == NULL) {
        return ERROR_BAD_ARGS;
    }
    uint8_t addr_buf[2] = {(uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF)};
    uint8_t data_buf[4] = {0};

    struct _i2c_m_msg wr = {
        .addr = STXG2_I2C_ADDR,
        .len = sizeof(addr_buf),
        .buffer = addr_buf,
        .flags = I2C_M_SEVEN, // No stop = repeated start
    };
    struct _i2c_m_msg rd = {
        .addr = STXG2_I2C_ADDR,
        .len = sizeof(data_buf),
        .buffer = data_buf,
        .flags = I2C_M_SEVEN | I2C_M_RD | I2C_M_STOP,
    };
    // No warning here because init polls this while the radio boots
    if (stxg2_i2c_transfer(&wr) != SUCCESS || stxg2_i2c_transfer(&rd) != SUCCESS) {
        return ERROR_I2C_FAILED;
    }
    *value = (uint32_t)data_buf[3] << 24 | (uint32_t)data_buf[2] << 16 | (uint32_t)data_buf[1] << 8 | (uint32_t)data_buf[0];
    return SUCCESS;
}

/**
 * \brief Radio power on/I2C + SPI enable/read reg
 */
status_t stxg2_init(void) {
    gpio_set_pin_level(SBAND_CS, true);
    gpio_set_pin_direction(SBAND_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(SBAND_CS, GPIO_PIN_FUNCTION_OFF);

    // Hold reset while power comes on (ICD 2.2)
    gpio_set_pin_level(SBAND_RST, false);
    gpio_set_pin_direction(SBAND_RST, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(SBAND_RST, GPIO_PIN_FUNCTION_OFF);

    gpio_set_pin_level(SBAND_EN, true);
    gpio_set_pin_direction(SBAND_EN, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(SBAND_EN, GPIO_PIN_FUNCTION_OFF);

    stxg2_delay_ms(STXG2_POWER_SETTLE_MS);
    gpio_set_pin_level(SBAND_RST, true);

    gpio_set_pin_pull_mode(SBAND_SDA, GPIO_PULL_UP);
    gpio_set_pin_pull_mode(SBAND_SCL, GPIO_PULL_UP);

    if (i2c_m_sync_enable(&I2C_SBAND) != 0) {
        warning("stxg2: failed to enable I2C bus\n");
        return ERROR_I2C_FAILED;
    }
    spi_m_sync_enable(&SPI_SBAND);

    // Reads fail until the radio boots
    uint32_t id = 0;
    status_t result = ERROR_NOT_READY;
    for (uint32_t waited = 0; waited <= STXG2_BOOT_TIMEOUT_MS; waited += STXG2_POLL_MS) {
        result = stxg2_read_reg(REG_ID, &id);
        if (result == SUCCESS) {
            break;
        }
        stxg2_delay_ms(STXG2_POLL_MS);
    }
    if (result == ERROR_I2C_FAILED) {
        warning("stxg2: no answer on I2C after %u ms (i2c error %ld)\n", STXG2_BOOT_TIMEOUT_MS, (long)last_i2c_error);
        return result;
    }
    if (result != SUCCESS) {
        warning("stxg2: ID read failed after %u ms (status %d)\n", STXG2_BOOT_TIMEOUT_MS, result);
        return result;
    }
    if (id != STXG2_ID_VALUE) {
        warning("stxg2: bad ID 0x%08lx\n", (unsigned long)id);
        return ERROR_SANITY_CHECK_FAILED;
    }

    // Clear boot flags, then test a write and read
    ret_err_status(stxg2_clear_i2c_status(), "stxg2: I2C status read failed\n");
    ret_err_status(stxg2_write_reg(REG_SCRATCHPAD, STXG2_SCRATCH_PATTERN), "stxg2: scratchpad write failed\n");
    uint32_t scratch = 0;
    ret_err_status(stxg2_read_reg(REG_SCRATCHPAD, &scratch), "stxg2: scratchpad read failed\n");
    if (scratch != STXG2_SCRATCH_PATTERN) {
        warning("stxg2: scratchpad read 0x%08lx\n", (unsigned long)scratch);
        return ERROR_SANITY_CHECK_FAILED;
    }
    store_active = false;
    return SUCCESS;
}

/**
 * \brief Sets mode and checks for mode transition (USM 9.2.5.1)
 *
 * \returns ERROR_PROCESSING_FAILED on mode error, ERROR_NOT_READY on timeout
 */
status_t stxg2_set_mode(stxg2_mode_t mode) {
    ret_err_status(stxg2_poll_reg(REG_MODE, MODE_BUSY, 0, STXG2_MODE_TIMEOUT_MS, NULL), "stxg2: mode busy\n");
    ret_err_status(stxg2_write_reg(REG_MODE, (uint32_t)mode), "stxg2: mode write failed\n");

    uint32_t value = 0;
    for (uint32_t waited = 0; waited <= STXG2_MODE_TIMEOUT_MS; waited += STXG2_POLL_MS) {
        // The radio does not answer I2C during the switch
        if (stxg2_read_reg(REG_MODE, &value) != SUCCESS) {
            stxg2_delay_ms(STXG2_POLL_MS);
            continue;
        }
        if (value & MODE_ERROR) {
            warning("stxg2: error changing to mode %u\n", (unsigned)mode);
            return ERROR_PROCESSING_FAILED;
        }
        if (!(value & MODE_BUSY) && ((value & MODE_MODE_MSK) == (uint32_t)mode)) {
            return SUCCESS;
        }
        stxg2_delay_ms(STXG2_POLL_MS);
    }
    warning("stxg2: timeout changing to mode %u (MODE = 0x%08lx)\n", (unsigned)mode, (unsigned long)value);
    return ERROR_NOT_READY;
}

/**
 * \brief Sets frequency, symbol rate, MODCOD, and PA power
 *
 * \param freq_khz 2400000-2480000, 100 kHz steps (option sheet, USM 9.2.5.3)
 * \param sym_rate_ksps 5000-50000, 1000 ksps steps (option sheet, USM 9.2.5.2)
 * \param modcod 1-28 (USM table 9-14)
 * \param power_dbm tx power in dbm (USM 9.2.9.2)
 * \returns ERROR_BAD_ARGS if out of range
 */
status_t stxg2_configure_rf(uint32_t freq_khz, uint32_t sym_rate_ksps, uint8_t modcod, uint8_t power_dbm) {
    if (freq_khz < STXG2_FREQ_MIN_KHZ || freq_khz > STXG2_FREQ_MAX_KHZ || (freq_khz % STXG2_FREQ_STEP_KHZ) != 0) {
        warning("stxg2: bad frequency %lu kHz\n", (unsigned long)freq_khz);
        return ERROR_BAD_ARGS;
    }
    if (sym_rate_ksps < STXG2_SYM_RATE_MIN_KSPS || sym_rate_ksps > STXG2_SYM_RATE_MAX_KSPS ||
        (sym_rate_ksps % STXG2_SYM_RATE_STEP_KSPS) != 0) {
        warning("stxg2: bad symbol rate %lu ksps\n", (unsigned long)sym_rate_ksps);
        return ERROR_BAD_ARGS;
    }
    if (modcod == 0 || modcod > DVBS2_MODCOD_MAX) {
        warning("stxg2: bad MODCOD %u\n", modcod);
        return ERROR_BAD_ARGS;
    }
    if (power_dbm < STXG2_POWER_MIN_DBM || power_dbm > STXG2_POWER_MAX_DBM) {
        warning("stxg2: bad PA power %u dBm\n", power_dbm);
        return ERROR_BAD_ARGS;
    }

    ret_err_status(stxg2_write_reg(REG_FREQUENCY, freq_khz), "stxg2: frequency write failed\n");
    ret_err_status(stxg2_write_reg(REG_SYMBOL_RATE, sym_rate_ksps), "stxg2: symbol rate write failed\n");

    uint32_t dvbs2 = 0;
    ret_err_status(stxg2_read_reg(REG_DVBS2_CONF, &dvbs2), "stxg2: DVB-S2 config read failed\n");
    dvbs2 = (dvbs2 & ~DVBS2_CONF_MODCOD_MSK) | ((uint32_t)modcod & DVBS2_CONF_MODCOD_MSK);
    ret_err_status(stxg2_write_reg(REG_DVBS2_CONF, dvbs2), "stxg2: DVB-S2 config write failed\n");

    return stxg2_write_reg(REG_PA_CONF_0, power_dbm);
}

/**
 * \brief Connects input_code (XBAR_IN_*) to the output at output_shift (XBAR_OUT_*_SHIFT) (USM 9.2.8.7)
 */
status_t stxg2_route(uint8_t input_code, uint8_t output_shift) {
    if (output_shift > XBAR_OUT_HS1_SHIFT || (output_shift % 4) != 0 || input_code > XBAR_IN_MEMRD) {
        return ERROR_BAD_ARGS;
    }
    uint32_t xbar = 0;
    ret_err_status(stxg2_read_reg(REG_CROSSBAR, &xbar), "stxg2: crossbar read failed\n");
    xbar &= ~(0xFu << output_shift);
    xbar |= ((uint32_t)input_code & 0xFu) << output_shift;
    return stxg2_write_reg(REG_CROSSBAR, xbar);
}

/**
 * \brief Sends len bytes over SPI in multiples of 4.
 */
static status_t stxg2_spi_send_words(const uint8_t *buf, size_t len) {
    size_t sent = 0;
    uint32_t waits = 0;
    uint32_t spi_status = 0;

    while (sent < len) {
        ret_err_status(stxg2_read_reg(REG_SPI_STATUS, &spi_status), "stxg2: SPI status read failed\n");
        if (spi_status & SPI_STATUS_OVERFLOW) {
            warning("stxg2: SPI buffer overflow, data lost\n");
            return ERROR_SPI_TRANSFER_FAILED;
        }

        // TR pin not wired, so poll Ready
        uint32_t used_words = SPI_STATUS_COUNT(spi_status);
        size_t room = 0;
        if ((spi_status & SPI_STATUS_READY) && !(spi_status & SPI_STATUS_FULL) && used_words < STXG2_SPI_BUF_WORDS) {
            room = (size_t)(STXG2_SPI_BUF_WORDS - used_words) * 4u;
        }
        if (room == 0) {
            if (++waits > STXG2_SPI_WAIT_LIMIT) {
                warning("stxg2: SPI buffer did not drain (SPI_STAT = 0x%08lx)\n", (unsigned long)spi_status);
                return ERROR_NOT_READY;
            }
            stxg2_delay_ms(1);
            continue;
        }
        waits = 0;

        size_t chunk = len - sent;
        if (chunk > room) {
            chunk = room;
        }
        if (chunk > STXG2_SPI_CHUNK_BYTES) {
            chunk = STXG2_SPI_CHUNK_BYTES;
        }
        chunk &= ~(size_t)3u; // Whole words

        memcpy(stxg2_spi_tx_buffer, buf + sent, chunk);
        stxg2_xfer.size = chunk;

        SBAND_CS_LOW();
        int32_t response = spi_m_sync_transfer(&SPI_SBAND, &stxg2_xfer);
        SBAND_CS_HIGH();

        if (response != (int32_t)chunk) {
            warning("stxg2: SPI transfer sent %ld of %u bytes\n", (long)response, (unsigned)chunk);
            return ERROR_SPI_TRANSFER_FAILED;
        }
        sent += chunk;
        store_words_sent += (uint32_t)(chunk / 4u);
    }
    return SUCCESS;
}

/**
 * \brief Disables SPI and disconnects memory write
 */
static void stxg2_store_cleanup(void) {
    store_active = false;
    store_carry_len = 0;
    if (stxg2_write_reg(REG_SPI_CTRL, 0) != SUCCESS) {
        warning("stxg2: SPI disable failed\n");
    }
    if (stxg2_route(XBAR_IN_NONE, XBAR_OUT_MEMWR_SHIFT) != SUCCESS) {
        warning("stxg2: crossbar disconnect failed\n");
    }
}

/**
 * \brief Starts a flash write from SPI at start_index (USM 12.1.2)
 */
status_t stxg2_store_begin(uint32_t start_index) {
    if (store_active) {
        warning("stxg2: store session already active\n");
        return ERROR_NOT_READY;
    }
    ret_err_status(stxg2_set_mode(STXG2_MODE_FLASH_ACCESS), "stxg2: flash access mode failed\n");
    ret_err_status(stxg2_wait_memory_idle(), "stxg2: memory not ready for write\n");
    ret_err_status(stxg2_check_index(start_index), "stxg2: bad write index\n");

    // Re-enable applies the mode and resets SPI_RX_CNT
    ret_err_status(stxg2_write_reg(REG_SPI_CTRL, 0), "stxg2: SPI disable failed\n");
    ret_err_status(stxg2_write_reg(REG_SPI_CTRL, SPI_CTRL_ENABLE | SPI_CTRL_MODE(STXG2_SPI_MODE)), "stxg2: SPI enable failed\n");

    ret_err_status(stxg2_write_reg(REG_MEM_WR_INDEX, start_index), "stxg2: write index failed\n");
    // Length 0 = until stop
    ret_err_status(stxg2_write_reg(REG_MEM_WR_LEN_0, 0), "stxg2: write length failed\n");
    ret_err_status(stxg2_write_reg(REG_MEM_WR_LEN_1, 0), "stxg2: write length failed\n");
    ret_err_status(stxg2_route(XBAR_IN_SPI, XBAR_OUT_MEMWR_SHIFT), "stxg2: route SPI to memory write failed\n");

    store_words_sent = 0;
    store_carry_len = 0;
    status_t result = stxg2_write_reg(REG_MEM_WR_CTRL, MEM_CTRL_START);
    if (result != SUCCESS) {
        warning("stxg2: memory write start failed\n");
        stxg2_store_cleanup();
        return result;
    }
    store_active = true;
    return SUCCESS;
}

/**
 * \brief Sends data to the active write
 *
 * \returns ERROR_NOT_READY if no write is active or the buffer does not drain
 */
status_t stxg2_store(const uint8_t *data_buf, size_t sz) {
    if (!store_active) {
        warning("stxg2: store called without store_begin\n");
        return ERROR_NOT_READY;
    }
    if (data_buf == NULL && sz > 0) {
        return ERROR_BAD_ARGS;
    }

    size_t offset = 0;

    // Finish the carried word
    if (store_carry_len > 0) {
        while (store_carry_len < 4 && offset < sz) {
            store_carry[store_carry_len++] = data_buf[offset++];
        }
        if (store_carry_len < 4) {
            return SUCCESS;
        }
        ret_err_status(stxg2_spi_send_words(store_carry, 4), "stxg2: SPI send failed\n");
        store_carry_len = 0;
    }

    size_t whole = ((sz - offset) / 4u) * 4u;
    if (whole > 0) {
        ret_err_status(stxg2_spi_send_words(data_buf + offset, whole), "stxg2: SPI send failed\n");
        offset += whole;
    }

    while (offset < sz) {
        store_carry[store_carry_len++] = data_buf[offset++];
    }
    return SUCCESS;
}

/**
 * \brief Ends active write and turns off SPI (USM 9.2.7.4, 9.2.7.7).
 *
 * \param packets_written Can be NULL
 * \returns first error
 */
status_t stxg2_store_end(uint32_t *packets_written) {
    if (!store_active) {
        return ERROR_NOT_READY;
    }
    status_t result = SUCCESS;

    if (store_carry_len > 0) {
        memset(&store_carry[store_carry_len], 0, 4u - store_carry_len);
        result = stxg2_spi_send_words(store_carry, 4);
        store_carry_len = 0;
    }

    // Wait for an empty SPI buffer
    if (result == SUCCESS) {
        result = stxg2_poll_reg(REG_SPI_STATUS, SPI_STATUS_COUNT_MSK, 0, STXG2_MEM_TIMEOUT_MS, NULL);
        if (result != SUCCESS) {
            warning("stxg2: SPI buffer did not empty\n");
        }
    }
    if (result == SUCCESS) {
        uint32_t rx_words = 0;
        result = stxg2_read_reg(REG_SPI_RX_CNT, &rx_words);
        if (result == SUCCESS && rx_words != store_words_sent) {
            warning("stxg2: radio got %lu SPI words, sent %lu\n", (unsigned long)rx_words, (unsigned long)store_words_sent);
            result = ERROR_SPI_TRANSFER_FAILED;
        }
    }

    status_t stop_result = stxg2_write_reg(REG_MEM_WR_CTRL, MEM_CTRL_STOP);
    uint32_t wr_stat = 0;
    if (stop_result == SUCCESS) {
        stop_result = stxg2_poll_reg(REG_MEM_WR_STAT, MEM_STAT_ACTIVE, 0, STXG2_MEM_TIMEOUT_MS, &wr_stat);
    }
    if (stop_result == ERROR_NOT_READY) {
        warning("stxg2: memory write did not stop, aborting\n");
        (void)stxg2_write_reg(REG_MEM_WR_CTRL, MEM_CTRL_ABORT);
    } else if (stop_result == SUCCESS) {
        if (wr_stat & (MEM_STAT_ERROR | MEM_STAT_OOB)) {
            uint32_t wr_err = 0;
            (void)stxg2_read_reg(REG_MEM_WR_ERR, &wr_err);
            warning("stxg2: memory write failed (MEM_WR_STAT = 0x%02lx, MEM_WR_ERR = 0x%08lx)\n", (unsigned long)wr_stat,
                    (unsigned long)wr_err);
            stop_result = ERROR_WRITE_FAILED;
        } else if (!(wr_stat & MEM_STAT_COMPLETE)) {
            warning("stxg2: memory write stopped without the Complete flag\n");
        }
    }
    if (result == SUCCESS) {
        result = stop_result;
    }

    if (packets_written != NULL) {
        *packets_written = 0;
        (void)stxg2_read_reg(REG_MEM_WR_PKT_CNT, packets_written);
    }

    stxg2_store_cleanup();
    return result;
}

/**
 * \brief Turns off the radio framer. (USM 9.2.8.1).
 *
 * \note On config enter, STX turns framer back on.
  */
static status_t stxg2_disable_framer(void) {
    uint32_t tf_ctrl = 0;
    ret_err_status(stxg2_read_reg(REG_TF_CTRL, &tf_ctrl), "stxg2: TF_CTRL read failed\n");
    if (!(tf_ctrl & TF_CTRL_ENABLE)) {
        return SUCCESS;
    }
    ret_err_status(stxg2_write_reg(REG_TF_CTRL, tf_ctrl & ~TF_CTRL_ENABLE), "stxg2: TF_CTRL write failed\n");
    ret_err_status(stxg2_read_reg(REG_TF_CTRL, &tf_ctrl), "stxg2: TF_CTRL read failed\n");
    if (tf_ctrl & TF_CTRL_ENABLE) {
        warning("stxg2: framer did not turn off (TF_CTRL = 0x%08lx)\n", (unsigned long)tf_ctrl);
        return ERROR_WRITE_FAILED;
    }
    return SUCCESS;
}

/**
 * \brief Transmits stored data without framing (USM 12.1.3).
 *
 * \param num_packets 0 = until EOF
 */
status_t stxg2_downlink_begin(uint32_t start_index, uint32_t num_packets) {
    if (store_active) {
        // Read and write cannot run together
        warning("stxg2: downlink requested during a store session\n");
        return ERROR_NOT_READY;
    }
    ret_err_status(stxg2_set_mode(STXG2_MODE_CONFIG), "stxg2: config mode failed\n");
    ret_err_status(stxg2_disable_framer(), "stxg2: framer bypass failed\n");
    ret_err_status(stxg2_wait_memory_idle(), "stxg2: memory not ready for read\n");
    ret_err_status(stxg2_check_index(start_index), "stxg2: bad read index\n");

    ret_err_status(stxg2_write_reg(REG_MEM_RD_INDEX, start_index), "stxg2: read index failed\n");
    ret_err_status(stxg2_write_reg(REG_MEM_RD_LEN, num_packets), "stxg2: read length failed\n");
    ret_err_status(stxg2_route(XBAR_IN_MEMRD, XBAR_OUT_FRAMER_SHIFT), "stxg2: route memory read to encoder failed\n");

    // Lock RF before the PA turns on
    uint32_t rf_status = 0;
    if (stxg2_poll_reg(REG_RF_STATUS, RF_STATUS_LOCKED, RF_STATUS_LOCKED, STXG2_MODE_TIMEOUT_MS, &rf_status) != SUCCESS) {
        warning("stxg2: RF synthesizers not locked (RF_STAT = 0x%lx)\n", (unsigned long)rf_status);
        return ERROR_NOT_READY;
    }
    ret_err_status(stxg2_set_mode(STXG2_MODE_TRANSMIT), "stxg2: transmit mode failed\n");

    // Framer must still be off
    status_t result = stxg2_disable_framer();
    if (result == SUCCESS) {
        uint32_t rd_ctrl = MEM_CTRL_START | (num_packets == 0 ? MEM_RD_CTRL_EOF_STOP : 0);
        result = stxg2_write_reg(REG_MEM_RD_CTRL, rd_ctrl);
    }
    if (result != SUCCESS) {
        warning("stxg2: downlink start failed, leaving transmit mode\n");
        (void)stxg2_set_mode(STXG2_MODE_CONFIG);
    }
    return result;
}

/**
 * \brief Stops read and retransmit
 *
 * \note Must pad end because internal framer not on
 */
status_t stxg2_downlink_end(void) {
    status_t result = stxg2_write_reg(REG_MEM_RD_CTRL, MEM_CTRL_STOP);
    if (result == SUCCESS) {
        result = stxg2_poll_reg(REG_MEM_RD_STAT, MEM_STAT_ACTIVE, 0, STXG2_MEM_TIMEOUT_MS, NULL);
    }
    if (result == ERROR_NOT_READY) {
        // Abort stops read and retransmit
        warning("stxg2: memory read did not stop, aborting\n");
        (void)stxg2_write_reg(REG_MEM_RD_CTRL, MEM_CTRL_ABORT);
    }
    (void)stxg2_write_reg(REG_MEM_RT_CTRL, MEM_RT_CTRL_STOP);

    status_t mode_result = stxg2_set_mode(STXG2_MODE_CONFIG);
    status_t framer_result = stxg2_disable_framer();
    status_t route_result = stxg2_route(XBAR_IN_NONE, XBAR_OUT_FRAMER_SHIFT);

    if (result != SUCCESS) {
        return result;
    }
    if (mode_result != SUCCESS) {
        return mode_result;
    }
    if (framer_result != SUCCESS) {
        return framer_result;
    }
    return route_result;
}

/**
 * \brief Queues and begins retransmit (USM 9.2.7.18)
 *
 * \param packet_id First memory index, 24 bits
 * \param count 1-255
 */
status_t stxg2_request_retransmit(uint32_t packet_id, uint8_t count) {
    if (count == 0 || packet_id > MEM_RT_QUEUE_INDEX_MSK) {
        return ERROR_BAD_ARGS;
    }
    ret_err_status(stxg2_check_index(packet_id), "stxg2: bad retransmit index\n");
    uint32_t req = (packet_id & MEM_RT_QUEUE_INDEX_MSK) | ((uint32_t)count << MEM_RT_QUEUE_LEN_POS);
    ret_err_status(stxg2_write_reg(REG_MEM_RT_QUEUE, req), "stxg2: retransmit queue write failed\n");

    uint32_t rt_ctrl = 0;
    ret_err_status(stxg2_read_reg(REG_MEM_RT_CTRL, &rt_ctrl), "stxg2: retransmit control read failed\n");
    rt_ctrl = (rt_ctrl & ~(MEM_RT_CTRL_STOP | MEM_RT_CTRL_CLEAR_QUEUE)) | MEM_RT_CTRL_START;
    return stxg2_write_reg(REG_MEM_RT_CTRL, rt_ctrl);
}

/**
 * \brief Reads BOARD_STAT (USM 9.2.4.1)
 */
status_t stxg2_get_board_status(uint32_t *status) {
    return stxg2_read_reg(REG_BOARD_STATUS, status);
}

/**
 * \brief Reads RF_STAT (USM 9.2.4.2)
 */
status_t stxg2_get_rf_status(uint32_t *status) {
    return stxg2_read_reg(REG_RF_STATUS, status);
}
