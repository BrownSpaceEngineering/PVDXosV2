/**
 * gyro_driver.c
 *
 * Driver for the Murata SCH16T-K01 gyroscope / accelerometer (SCH1 family), reached over
 * SPI on SERCOM4 (SPI_MAGNETOMETER_GYRO), with chip-select on GYRO_CS and the active-low
 * EXTRESN reset on GYRO_RST.
 *
 * Created: September 27, 2026
 * Ported from Murata's SCH1 reference library and the PVDX SCH16T Arduino prototype.
 */

//****************************************************************************************
// The SCH1 register map, frame protocol and conversion routines in this file are derived
// from Murata's SCH1 reference library, released under the BSD license as follows.
//
// Copyright (c) 2024, Murata Electronics Oy.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following
// conditions are met:
//    1. Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//    2. Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials
//       provided with the distribution.
//    3. Neither the name of Murata Electronics Oy nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//****************************************************************************************

#include "gyro_driver.h"

#include "globals.h"
#include "hal_delay.h"

// The configuration from gyro_driver.h, in the shape the setter functions want it
static const SCH1_filter_t gyro_filter = {
    .Rate12 = SCH16T_FILTER_RATE12,
    .Acc12 = SCH16T_FILTER_ACC12,
    .Acc3 = SCH16T_FILTER_ACC3,
};

static const SCH1_sensitivity_t gyro_sensitivity = {
    .Rate1 = SCH16T_SENSITIVITY_RATE1,
    .Rate2 = SCH16T_SENSITIVITY_RATE2,
    .Acc1 = SCH16T_SENSITIVITY_ACC1,
    .Acc2 = SCH16T_SENSITIVITY_ACC2,
    .Acc3 = SCH16T_SENSITIVITY_ACC3,
};

static const SCH1_decimation_t gyro_decimation = {
    .Rate2 = SCH16T_DECIMATION_RATE2,
    .Acc2 = SCH16T_DECIMATION_ACC2,
};

// Buffers for SPI transactions. The sensor transfers exactly one 48-bit frame at a time.
static uint8_t spi_rx_buffer[SCH16T_FRAME_SIZE_BYTES] = {0x00};
static uint8_t spi_tx_buffer[SCH16T_FRAME_SIZE_BYTES] = {0x00};
static struct spi_xfer gyro_xfer = {.rxbuf = spi_rx_buffer, .txbuf = spi_tx_buffer, .size = SCH16T_FRAME_SIZE_BYTES};

// The registers gyro_read() pulls, in the order their answers come back
#define GYRO_MEASUREMENT_FRAME_COUNT 7
static const uint64_t gyro_measurement_requests[GYRO_MEASUREMENT_FRAME_COUNT] = {
    SCH16T_REQ_READ_RATE_X1, SCH16T_REQ_READ_RATE_Y1, SCH16T_REQ_READ_RATE_Z1, SCH16T_REQ_READ_ACC_X1,
    SCH16T_REQ_READ_ACC_Y1,  SCH16T_REQ_READ_ACC_Z1,  SCH16T_REQ_READ_TEMP,
};

// The status registers gyro_get_status() pulls, in SCH1_status_t field order
#define GYRO_STATUS_FRAME_COUNT 10
static const uint64_t gyro_status_requests[GYRO_STATUS_FRAME_COUNT] = {
    SCH16T_REQ_READ_STAT_SUM,    SCH16T_REQ_READ_STAT_SUM_SAT, SCH16T_REQ_READ_STAT_COM,    SCH16T_REQ_READ_STAT_RATE_COM,
    SCH16T_REQ_READ_STAT_RATE_X, SCH16T_REQ_READ_STAT_RATE_Y,  SCH16T_REQ_READ_STAT_RATE_Z, SCH16T_REQ_READ_STAT_ACC_X,
    SCH16T_REQ_READ_STAT_ACC_Y,  SCH16T_REQ_READ_STAT_ACC_Z,
};

/* ---------- HARDWARE LAYER ---------- */

/**
 * \fn gyro_delay_ms
 *
 * \brief Blocks for the given number of milliseconds, whether or not the RTOS is running yet
 *
 * \param ms how long to wait, in milliseconds
 *
 * \note init_gyro_hardware() is reached from check_all_devices_on_startup(), which runs in
 *       main() before vTaskStartScheduler(). vTaskDelay() cannot be used there, but the
 *       SysTick-based delay_ms() must not be used once FreeRTOS owns SysTick, so pick per call.
 */
static void gyro_delay_ms(uint16_t ms) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        delay_ms(ms);
    } else {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/**
 * \fn gyro_crc8
 *
 * \brief Calculates the CRC8 of a 48-bit SPI frame, over every bit but the CRC field itself
 *
 * \param frame the 48-bit SPI frame to calculate the CRC of
 *
 * \returns the CRC of the given frame
 */
static uint8_t gyro_crc8(uint64_t frame) {
    uint64_t data = frame & 0xFFFFFFFFFF00ULL; // the 40 address and payload bits
    uint8_t crc = 0xFF;

    for (int8_t i = 47; i >= 0; i--) {
        uint8_t data_bit = (data >> i) & 0x01;
        crc = crc & 0x80 ? (uint8_t)((crc << 1) ^ 0x2F) ^ data_bit : (uint8_t)(crc << 1) | data_bit;
    }

    return crc;
}

/**
 * \fn gyro_spi_request
 *
 * \brief Performs a single 48-bit SPI transfer to the gyro
 *
 * \param request the 48-bit MOSI frame to send
 * \param p_response if not NULL, set to the 48-bit MISO frame received during the same transfer
 *
 * \returns `status_t`, whether the transfer operation was successful
 *
 * \warning returns `ERROR_SPI_TRANSFER_FAILED` if transfer unsuccessful
 */
static status_t gyro_spi_request(uint64_t request, uint64_t *const p_response) {
    // The sensor clocks frames out MSB first, so byte 0 carries bits 47..40
    for (uint8_t i = 0; i < SCH16T_FRAME_SIZE_BYTES; i++) {
        spi_tx_buffer[i] = (uint8_t)(request >> (8 * (SCH16T_FRAME_SIZE_BYTES - 1 - i)));
    }

    GYRO_CS_LOW(); // select the gyro for SPI communication

    int32_t response = spi_m_sync_transfer(&SPI_MAGNETOMETER_GYRO, &gyro_xfer);

    GYRO_CS_HIGH(); // deselect the gyro for SPI communication

    if (response != (int32_t)gyro_xfer.size) {
        return ERROR_SPI_TRANSFER_FAILED;
    }

    if (p_response != NULL) {
        uint64_t received = 0;
        for (uint8_t i = 0; i < SCH16T_FRAME_SIZE_BYTES; i++) {
            received = (received << 8) | spi_rx_buffer[i];
        }
        *p_response = received;
    }

    return SUCCESS;
}

/**
 * \fn gyro_read_registers
 *
 * \brief Reads a series of registers from the gyro
 *
 * \param requests array of `count` read-request frames
 * \param responses array of `count` frames to fill; responses[i] answers requests[i]
 * \param count how many registers to read
 *
 * \returns `status_t`, whether every transfer was successful
 *
 * \note The SCH16T uses an "off-frame" protocol: the answer to a request arrives in the
 *       *following* transfer, not the one that carried it. Reading `count` registers therefore
 *       takes `count + 1` transfers. The first primes the pipeline and its response is thrown
 *       away, and the last repeats the final request purely to clock that request's answer out.
 *       This is why the request and response of a given transfer never line up below.
 */
static status_t gyro_read_registers(const uint64_t *const requests, uint64_t *const responses, size_t count) {
    ret_err_status(gyro_spi_request(requests[0], NULL), "gyro: could not prime the read pipeline\n");

    for (size_t i = 0; i < count; i++) {
        // Carry the next request out while the previous request's answer comes back
        const uint64_t next_request = (i + 1 < count) ? requests[i + 1] : requests[count - 1];
        ret_err_status(gyro_spi_request(next_request, &responses[i]), "gyro: register read failed\n");
    }

    return SUCCESS;
}

/* ---------- FRAME VALIDATION ---------- */

/**
 * \fn gyro_verify_response
 *
 * \brief Checks that a response frame actually came from the register the request addressed
 *
 * \param request the request frame that was sent
 * \param response the response frame that came back
 *
 * \returns `status_t`, `ERROR_SPI_TRANSFER_FAILED` if the sensor did not answer or answered
 *          from the wrong register
 */
static status_t gyro_verify_response(uint64_t request, uint64_t response) {
    // A blank frame means the sensor did not answer at all
    if ((response == SCH16T_FRAME_ALL_ONES) || (response == SCH16T_FRAME_ALL_ZEROES)) {
        return ERROR_SPI_TRANSFER_FAILED;
    }

    // The response's Source Address must match the request's Target Address
    if (((request & SCH16T_TA_FIELD_MASK) >> SCH16T_TA_FIELD_SHIFT) != ((response & SCH16T_SA_FIELD_MASK) >> SCH16T_SA_FIELD_SHIFT)) {
        return ERROR_SPI_TRANSFER_FAILED;
    }

    return SUCCESS;
}

/**
 * \fn gyro_verify_write
 *
 * \brief Checks that a register write took effect, by comparing the frame that was written
 *        against the frame read back out of the same register
 *
 * \param request the request frame that performed the write
 * \param response the response frame read back from the register
 *
 * \returns `status_t`, `ERROR_SPI_TRANSFER_FAILED` if the sensor did not answer, answered from
 *          the wrong register, or is not holding the value that was written
 */
static status_t gyro_verify_write(uint64_t request, uint64_t response) {
    ret_err_status(gyro_verify_response(request, response), "gyro: register write was not acknowledged\n");

    if ((request & SCH16T_DATA_FIELD_MASK) != (response & SCH16T_DATA_FIELD_MASK)) {
        return ERROR_SPI_TRANSFER_FAILED;
    }

    return SUCCESS;
}

/**
 * \fn gyro_write_register
 *
 * \brief Builds a write frame from a base request and a payload, CRCs it, and sends it
 *
 * \param request_base the SCH16T_REQ_SET_* base frame for the register being written
 * \param data_field the payload to write into the register
 * \param p_request_frame if not NULL, set to the complete frame that was sent, so that it can
 *        later be handed to gyro_verify_write()
 *
 * \returns `status_t`, whether the transfer operation was successful
 */
static status_t gyro_write_register(uint64_t request_base, uint64_t data_field, uint64_t *const p_request_frame) {
    uint64_t frame = request_base | data_field;
    frame <<= 8; // make room for the CRC, which covers everything above it
    frame |= gyro_crc8(frame);

    if (p_request_frame != NULL) {
        *p_request_frame = frame;
    }

    return gyro_spi_request(frame, NULL);
}

/* ---------- CONFIGURATION VALUE CONVERSION ---------- */

/**
 * \fn gyro_is_valid_filter_freq
 *
 * \brief Checks whether a filter corner frequency is one the sensor supports
 *
 * \param freq filter corner frequency, in Hz; SCH16T_FILTER_BYPASS means bypass mode
 *
 * \returns whether the frequency is valid
 */
static bool gyro_is_valid_filter_freq(uint32_t freq) {
    return freq == 13 || freq == 30 || freq == 68 || freq == 235 || freq == 280 || freq == 370 || freq == SCH16T_FILTER_BYPASS;
}

/**
 * \fn gyro_is_valid_rate_sens
 *
 * \brief Checks whether a rate sensitivity is one the sensor supports
 *
 * \param sens sensitivity, in LSB/dps
 *
 * \returns whether the sensitivity is valid
 */
static bool gyro_is_valid_rate_sens(uint32_t sens) {
    return sens == 1600 || sens == 3200 || sens == 6400;
}

/**
 * \fn gyro_is_valid_acc_sens
 *
 * \brief Checks whether an accelerometer sensitivity is one the sensor supports
 *
 * \param sens sensitivity, in LSB/(m/s^2)
 *
 * \returns whether the sensitivity is valid
 */
static bool gyro_is_valid_acc_sens(uint32_t sens) {
    return sens == 3200 || sens == 6400 || sens == 12800 || sens == 25600;
}

/**
 * \fn gyro_is_valid_decimation
 *
 * \brief Checks whether an output sample rate divider is one the sensor supports
 *
 * \param decimation decimation ratio
 *
 * \returns whether the decimation ratio is valid
 */
static bool gyro_is_valid_decimation(uint32_t decimation) {
    return decimation == 2 || decimation == 4 || decimation == 8 || decimation == 16 || decimation == 32;
}

/**
 * \fn gyro_filter_bitfield
 *
 * \brief Converts a filter corner frequency into its register bitfield
 *
 * \param freq filter corner frequency, in Hz
 *
 * \returns the bitfield for building the filter setting frame, with all three axes set alike
 */
static uint32_t gyro_filter_bitfield(uint32_t freq) {
    switch (freq) {
        case 13:
            return 0x092; // 010 010 010
        case 30:
            return 0x049; // 001 001 001
        case 68:
            return 0x000; // 000 000 000
        case 235:
            return 0x16D; // 101 101 101
        case 280:
            return 0x0DB; // 011 011 011
        case 370:
            return 0x124; // 100 100 100
        case SCH16T_FILTER_BYPASS:
            return 0x1FF; // 111 111 111, filter bypass mode
        default:
            return 0x000;
    }
}

/**
 * \fn gyro_rate_sens_bitfield
 *
 * \brief Converts a rate sensitivity into its RATE_CTRL register bitfield
 *
 * \param sens sensitivity, in LSB/dps
 *
 * \returns the bitfield for building the rate sensitivity setting frame
 */
static uint32_t gyro_rate_sens_bitfield(uint32_t sens) {
    switch (sens) {
        case 1600:
            return 0x02; // 010
        case 3200:
            return 0x03; // 011
        case 6400:
            return 0x04; // 100
        default:
            return 0x01;
    }
}

/**
 * \fn gyro_acc_sens_bitfield
 *
 * \brief Converts an accelerometer sensitivity into its ACC12/ACC3_CTRL register bitfield
 *
 * \param sens sensitivity, in LSB/(m/s^2)
 *
 * \returns the bitfield for building the acceleration sensitivity setting frame
 */
static uint32_t gyro_acc_sens_bitfield(uint32_t sens) {
    switch (sens) {
        case 3200:
            return 0x01; // 001
        case 6400:
            return 0x02; // 010
        case 12800:
            return 0x03; // 011
        case 25600:
            return 0x04; // 100
        default:
            return 0x00;
    }
}

/**
 * \fn gyro_decimation_bitfield
 *
 * \brief Converts an output sample rate divider into its register bitfield
 *
 * \param decimation decimation ratio
 *
 * \returns the bitfield for building the decimation setting frame
 */
static uint32_t gyro_decimation_bitfield(uint32_t decimation) {
    switch (decimation) {
        case 2:
            return 0x00;
        case 4:
            return 0x01;
        case 8:
            return 0x02;
        case 16:
            return 0x03;
        case 32:
            return 0x04;
        default:
            return 0x00;
    }
}

/* ---------- CONFIGURATION ---------- */

/**
 * \fn gyro_set_filters
 *
 * \brief Sets the low-pass filter corner frequency of every output channel
 *
 * \param freq_rate12 filter for the Rate_XYZ1 (interpolated) and Rate_XYZ2 (decimated) outputs
 * \param freq_acc12 filter for the Acc_XYZ1 (interpolated) and Acc_XYZ2 (decimated) outputs
 * \param freq_acc3 filter for the Acc_XYZ3 (interpolated) output
 *
 * \returns `status_t`, whether every filter was set and read back intact
 *
 * \warning returns `ERROR_SANITY_CHECK_FAILED` if any frequency is unsupported
 */
static status_t gyro_set_filters(uint32_t freq_rate12, uint32_t freq_acc12, uint32_t freq_acc3) {
    if (!gyro_is_valid_filter_freq(freq_rate12) || !gyro_is_valid_filter_freq(freq_acc12) || !gyro_is_valid_filter_freq(freq_acc3)) {
        warning("gyro: unsupported filter frequency\n");
        return ERROR_SANITY_CHECK_FAILED;
    }

    uint64_t requests[3];
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_FILT_RATE, gyro_filter_bitfield(freq_rate12), &requests[0]),
                   "gyro: could not set the Rate_XYZ1/2 filter\n");
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_FILT_ACC12, gyro_filter_bitfield(freq_acc12), &requests[1]),
                   "gyro: could not set the Acc_XYZ1/2 filter\n");
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_FILT_ACC3, gyro_filter_bitfield(freq_acc3), &requests[2]),
                   "gyro: could not set the Acc_XYZ3 filter\n");

    const uint64_t readbacks[3] = {SCH16T_REQ_READ_FILT_RATE, SCH16T_REQ_READ_FILT_ACC12, SCH16T_REQ_READ_FILT_ACC3};
    uint64_t responses[3];
    ret_err_status(gyro_read_registers(readbacks, responses, 3), "gyro: could not read the filter registers back\n");

    for (uint8_t i = 0; i < 3; i++) {
        ret_err_status(gyro_verify_write(requests[i], responses[i]), "gyro: filter register did not hold what was written\n");
    }

    return SUCCESS;
}

/**
 * \fn gyro_set_rate_sens_dec
 *
 * \brief Sets the Rate_XYZ1 and Rate_XYZ2 sensitivities and the Rate_XYZ2 decimation
 *
 * \param sens_rate1 sensitivity for the Rate_XYZ1 (interpolated) output
 * \param sens_rate2 sensitivity for the Rate_XYZ2 (decimated) output
 * \param dec_rate2 decimation for the Rate_XYZ2 output (FPRIM / dec_rate2), used for all axes
 *
 * \returns `status_t`, whether the register was set and read back intact
 *
 * \warning returns `ERROR_SANITY_CHECK_FAILED` if any value is unsupported
 */
static status_t gyro_set_rate_sens_dec(uint16_t sens_rate1, uint16_t sens_rate2, uint16_t dec_rate2) {
    if (!gyro_is_valid_rate_sens(sens_rate1) || !gyro_is_valid_rate_sens(sens_rate2) || !gyro_is_valid_decimation(dec_rate2)) {
        warning("gyro: unsupported rate sensitivity or decimation\n");
        return ERROR_SANITY_CHECK_FAILED;
    }

    // RATE_CTRL packs, from the top down: Rate1 sensitivity, Rate2 sensitivity, then the
    // Rate2 decimation once per axis
    const uint32_t decimation_bits = gyro_decimation_bitfield(dec_rate2);
    uint32_t data_field = gyro_rate_sens_bitfield(sens_rate1);
    data_field = (data_field << 3) | gyro_rate_sens_bitfield(sens_rate2);
    data_field = (data_field << 3) | decimation_bits;
    data_field = (data_field << 3) | decimation_bits;
    data_field = (data_field << 3) | decimation_bits;

    uint64_t request;
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_RATE_CTRL, data_field, &request),
                   "gyro: could not set the rate sensitivity and decimation\n");

    const uint64_t readback = SCH16T_REQ_READ_RATE_CTRL;
    uint64_t response;
    ret_err_status(gyro_read_registers(&readback, &response, 1), "gyro: could not read RATE_CTRL back\n");
    ret_err_status(gyro_verify_write(request, response), "gyro: RATE_CTRL did not hold what was written\n");

    return SUCCESS;
}

/**
 * \fn gyro_set_acc_sens_dec
 *
 * \brief Sets the Acc_XYZ1, Acc_XYZ2 and Acc_XYZ3 sensitivities and the Acc_XYZ2 decimation
 *
 * \param sens_acc1 sensitivity for the Acc_XYZ1 (interpolated) output
 * \param sens_acc2 sensitivity for the Acc_XYZ2 (decimated) output
 * \param sens_acc3 sensitivity for the Acc_XYZ3 (interpolated) output
 * \param dec_acc2 decimation for the Acc_XYZ2 output (FPRIM / dec_acc2), used for all axes
 *
 * \returns `status_t`, whether both registers were set and read back intact
 *
 * \warning returns `ERROR_SANITY_CHECK_FAILED` if any value is unsupported
 */
static status_t gyro_set_acc_sens_dec(uint16_t sens_acc1, uint16_t sens_acc2, uint16_t sens_acc3, uint16_t dec_acc2) {
    if (!gyro_is_valid_acc_sens(sens_acc1) || !gyro_is_valid_acc_sens(sens_acc2) || !gyro_is_valid_acc_sens(sens_acc3) ||
        !gyro_is_valid_decimation(dec_acc2)) {
        warning("gyro: unsupported acceleration sensitivity or decimation\n");
        return ERROR_SANITY_CHECK_FAILED;
    }

    // ACC12_CTRL is packed the same way as RATE_CTRL
    const uint32_t decimation_bits = gyro_decimation_bitfield(dec_acc2);
    uint32_t data_field = gyro_acc_sens_bitfield(sens_acc1);
    data_field = (data_field << 3) | gyro_acc_sens_bitfield(sens_acc2);
    data_field = (data_field << 3) | decimation_bits;
    data_field = (data_field << 3) | decimation_bits;
    data_field = (data_field << 3) | decimation_bits;

    uint64_t requests[2];
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_ACC12_CTRL, data_field, &requests[0]),
                   "gyro: could not set the Acc_XYZ1/2 sensitivity and decimation\n");
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_ACC3_CTRL, gyro_acc_sens_bitfield(sens_acc3), &requests[1]),
                   "gyro: could not set the Acc_XYZ3 sensitivity\n");

    const uint64_t readbacks[2] = {SCH16T_REQ_READ_ACC12_CTRL, SCH16T_REQ_READ_ACC3_CTRL};
    uint64_t responses[2];
    ret_err_status(gyro_read_registers(readbacks, responses, 2), "gyro: could not read the acceleration control registers back\n");

    for (uint8_t i = 0; i < 2; i++) {
        ret_err_status(gyro_verify_write(requests[i], responses[i]), "gyro: acceleration control register did not hold what was written\n");
    }

    return SUCCESS;
}

/**
 * \fn gyro_set_dry
 *
 * \brief Configures the sensor's Data Ready (DRY) pin, which is wired to GYRO_DRDY
 *
 * \param active_low false for an active-high DRY pin (the sensor's default), true for active-low
 * \param enable whether to drive the DRY pin at all
 *
 * \returns `status_t`, whether USER_IF_CTRL was set and read back intact
 */
static status_t gyro_set_dry(bool active_low, bool enable) {
    // DRY shares USER_IF_CTRL with settings this driver does not touch, so read-modify-write
    const uint64_t readback = SCH16T_REQ_READ_USER_IF_CTRL;
    uint64_t response;
    ret_err_status(gyro_read_registers(&readback, &response, 1), "gyro: could not read USER_IF_CTRL\n");

    uint64_t data_field = (response & SCH16T_DATA_FIELD_MASK) >> 8;

    if (active_low) {
        data_field |= SCH16T_USER_IF_DRY_POLARITY;
    } else {
        data_field &= ~(uint64_t)SCH16T_USER_IF_DRY_POLARITY;
    }

    if (enable) {
        data_field |= SCH16T_USER_IF_DRY_ENABLE;
    } else {
        data_field &= ~(uint64_t)SCH16T_USER_IF_DRY_ENABLE;
    }

    uint64_t request;
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_USER_IF_CTRL, data_field, &request), "gyro: could not set USER_IF_CTRL\n");

    ret_err_status(gyro_read_registers(&readback, &response, 1), "gyro: could not read USER_IF_CTRL back\n");
    ret_err_status(gyro_verify_write(request, response), "gyro: USER_IF_CTRL did not hold what was written\n");

    return SUCCESS;
}

/**
 * \fn gyro_enable_meas
 *
 * \brief Starts or stops measurement mode, and optionally ends initialization
 *
 * \param enable_sensor whether to enable the sensor
 * \param set_eoi whether to set the End Of Initialization bit, which locks every R/W register
 *        except the soft reset. It only takes effect when the common status reports no errors.
 *
 * \returns `status_t`, whether MODE_CTRL was set and acknowledged
 *
 * \note Unlike the other setters this does not compare the payload read back against the one
 *       written, because the sensor does not necessarily report EOI back the way it was sent.
 */
static status_t gyro_enable_meas(bool enable_sensor, bool set_eoi) {
    uint64_t data_field = 0;

    if (enable_sensor) {
        data_field |= SCH16T_MODE_EN_SENSOR;
    }
    if (set_eoi) {
        data_field |= SCH16T_MODE_EOI_CTRL;
    }

    uint64_t request;
    ret_err_status(gyro_write_register(SCH16T_REQ_SET_MODE_CTRL, data_field, &request), "gyro: could not set MODE_CTRL\n");

    const uint64_t readback = SCH16T_REQ_READ_MODE_CTRL;
    uint64_t response;
    ret_err_status(gyro_read_registers(&readback, &response, 1), "gyro: could not read MODE_CTRL back\n");
    ret_err_status(gyro_verify_response(request, response), "gyro: MODE_CTRL write was not acknowledged\n");

    return SUCCESS;
}

/* ---------- MEASUREMENT AND STATUS ---------- */

/**
 * \fn gyro_get_status
 *
 * \brief Reads every status register out of the sensor
 *
 * \param p_status pointer to an `SCH1_status_t` to fill
 *
 * \returns `status_t`, whether the read was successful
 *
 * \warning returns `ERROR_SANITY_CHECK_FAILED` if `p_status` is NULL
 */
status_t gyro_get_status(SCH1_status_t *const p_status) {
    if (p_status == NULL) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    uint64_t responses[GYRO_STATUS_FRAME_COUNT];
    ret_err_status(gyro_read_registers(gyro_status_requests, responses, GYRO_STATUS_FRAME_COUNT),
                   "gyro: could not read the status registers\n");

    p_status->Summary = SCH16T_SPI48_DATA_UINT16(responses[0]);
    p_status->Summary_Sat = SCH16T_SPI48_DATA_UINT16(responses[1]);
    p_status->Common = SCH16T_SPI48_DATA_UINT16(responses[2]);
    p_status->Rate_Common = SCH16T_SPI48_DATA_UINT16(responses[3]);
    p_status->Rate_X = SCH16T_SPI48_DATA_UINT16(responses[4]);
    p_status->Rate_Y = SCH16T_SPI48_DATA_UINT16(responses[5]);
    p_status->Rate_Z = SCH16T_SPI48_DATA_UINT16(responses[6]);
    p_status->Acc_X = SCH16T_SPI48_DATA_UINT16(responses[7]);
    p_status->Acc_Y = SCH16T_SPI48_DATA_UINT16(responses[8]);
    p_status->Acc_Z = SCH16T_SPI48_DATA_UINT16(responses[9]);

    return SUCCESS;
}

/**
 * \fn gyro_status_ok
 *
 * \brief Checks whether every status register reports a healthy sensor
 *
 * \param p_status pointer to a filled-in `SCH1_status_t`
 *
 * \returns whether all status registers are OK; false if `p_status` is NULL
 */
bool gyro_status_ok(const SCH1_status_t *const p_status) {
    if (p_status == NULL) {
        return false;
    }

    return p_status->Summary == SCH16T_STATUS_REGISTER_OK && p_status->Summary_Sat == SCH16T_STATUS_REGISTER_OK &&
        p_status->Common == SCH16T_STATUS_REGISTER_OK && p_status->Rate_Common == SCH16T_STATUS_REGISTER_OK &&
        p_status->Rate_X == SCH16T_STATUS_REGISTER_OK && p_status->Rate_Y == SCH16T_STATUS_REGISTER_OK &&
        p_status->Rate_Z == SCH16T_STATUS_REGISTER_OK && p_status->Acc_X == SCH16T_STATUS_REGISTER_OK &&
        p_status->Acc_Y == SCH16T_STATUS_REGISTER_OK && p_status->Acc_Z == SCH16T_STATUS_REGISTER_OK;
}

/**
 * \fn gyro_get_data
 *
 * \brief Reads rate, acceleration and temperature counts out of the sensor
 *
 * \param data pointer to an `SCH1_raw_data_t` to fill
 *
 * \returns `status_t`, whether every transfer was successful. Note that a successful read can
 *          still carry `data->frame_error`, which means the sensor flagged the readings
 *          themselves as untrustworthy.
 */
static status_t gyro_get_data(SCH1_raw_data_t *const data) {
    uint64_t responses[GYRO_MEASUREMENT_FRAME_COUNT];
    ret_err_status(gyro_read_registers(gyro_measurement_requests, responses, GYRO_MEASUREMENT_FRAME_COUNT),
                   "gyro: could not read measurement registers\n");

    // Every response carries an error field the sensor sets when the reading cannot be trusted.
    // TODO: each frame also carries a CRC8 that gyro_crc8() could check here. The Arduino
    // prototype this was ported from only ever checked the error field, so that is all this
    // does for now; add the CRC check once the port is confirmed working on real hardware.
    data->frame_error = false;
    for (uint8_t i = 0; i < GYRO_MEASUREMENT_FRAME_COUNT; i++) {
        if (responses[i] & SCH16T_ERROR_FIELD_MASK) {
            data->frame_error = true;
            break;
        }
    }

    data->Rate1_raw[0] = SCH16T_SPI48_DATA_INT32(responses[0]);
    data->Rate1_raw[1] = SCH16T_SPI48_DATA_INT32(responses[1]);
    data->Rate1_raw[2] = SCH16T_SPI48_DATA_INT32(responses[2]);
    data->Acc1_raw[0] = SCH16T_SPI48_DATA_INT32(responses[3]);
    data->Acc1_raw[1] = SCH16T_SPI48_DATA_INT32(responses[4]);
    data->Acc1_raw[2] = SCH16T_SPI48_DATA_INT32(responses[5]);

    // Temperature data is always 16 bits wide, so drop the 4 unused LSBs
    data->Temp_raw = SCH16T_SPI48_DATA_INT32(responses[6]) >> 4;

    return SUCCESS;
}

/**
 * \fn gyro_convert_data
 *
 * \brief Scales raw sensor counts into dps, m/s^2 and degrees Celsius
 *
 * \param data_in pointer to raw counts from the sensor
 * \param data_out pointer to an `SCH1_result_t` to fill with scaled values
 */
static void gyro_convert_data(const SCH1_raw_data_t *const data_in, SCH1_result_t *const data_out) {
    for (uint8_t i = 0; i < 3; i++) {
        data_out->Rate1[i] = (float)data_in->Rate1_raw[i] / (float)gyro_sensitivity.Rate1;
        data_out->Acc1[i] = (float)data_in->Acc1_raw[i] / (float)gyro_sensitivity.Acc1;
    }

    data_out->Temp = (float)data_in->Temp_raw / SCH16T_TEMPERATURE_LSB_PER_DEGREE;
}

/**
 * \fn gyro_read
 *
 * \brief Reads angular rate, acceleration and temperature from the gyro
 *
 * \param result pointer to an `SCH1_result_t` to fill with scaled readings. Rate1 is in dps,
 *        Acc1 is in m/s^2 and Temp is in degrees Celsius; indices are x, y, z.
 *
 * \returns `status_t` SUCCESS if the read was successful, `ERROR_SANITY_CHECK_FAILED` if
 *          `result` is NULL, `ERROR_SPI_TRANSFER_FAILED` on a SPI communication error, and
 *          `ERROR_READ_FAILED` if the sensor flagged the readings as untrustworthy.
 *
 * \note `result` is still filled in when `ERROR_READ_FAILED` is returned, so a caller that
 *       wants to look at a suspect reading can, but it should not be trusted as a measurement.
 */
status_t gyro_read(SCH1_result_t *const result) {
    if (result == NULL) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    SCH1_raw_data_t raw_data;
    ret_err_status(gyro_get_data(&raw_data), "gyro: read failed\n");

    gyro_convert_data(&raw_data, result);

    if (raw_data.frame_error) {
        warning("gyro: sensor flagged a measurement frame as containing an error\n");
        return ERROR_READ_FAILED;
    }

    return SUCCESS;
}

/**
 * \fn gyro_get_serial_number
 *
 * \brief Reads the sensor's serial number as a NUL-terminated string
 *
 * \param p_buffer buffer to write the serial number into
 * \param buffer_size size of `p_buffer`; must be at least `SCH16T_SERIAL_NUMBER_SIZE`
 *
 * \returns `status_t`, whether the read was successful
 *
 * \warning returns `ERROR_SANITY_CHECK_FAILED` if `p_buffer` is NULL or too small
 */
status_t gyro_get_serial_number(char *const p_buffer, size_t buffer_size) {
    if (p_buffer == NULL || buffer_size < SCH16T_SERIAL_NUMBER_SIZE) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    const uint64_t requests[3] = {SCH16T_REQ_READ_SN_ID1, SCH16T_REQ_READ_SN_ID2, SCH16T_REQ_READ_SN_ID3};
    uint64_t responses[3];
    ret_err_status(gyro_read_registers(requests, responses, 3), "gyro: could not read the serial number registers\n");

    const uint16_t sn_id1 = SCH16T_SPI48_DATA_UINT16(responses[0]);
    const uint16_t sn_id2 = SCH16T_SPI48_DATA_UINT16(responses[1]);
    const uint16_t sn_id3 = SCH16T_SPI48_DATA_UINT16(responses[2]);

    // The serial number is formatted "%05u%01X%04X" of sn_id2, sn_id1 and sn_id3. We have no
    // snprintf here, so build it a digit at a time.
    static const char hex_digits[] = "0123456789ABCDEF";
    p_buffer[0] = (char)((sn_id2 / 10000) + '0');
    p_buffer[1] = (char)(((sn_id2 / 1000) % 10) + '0');
    p_buffer[2] = (char)(((sn_id2 / 100) % 10) + '0');
    p_buffer[3] = (char)(((sn_id2 / 10) % 10) + '0');
    p_buffer[4] = (char)((sn_id2 % 10) + '0');
    p_buffer[5] = hex_digits[sn_id1 & 0xF];
    p_buffer[6] = hex_digits[(sn_id3 >> 12) & 0xF];
    p_buffer[7] = hex_digits[(sn_id3 >> 8) & 0xF];
    p_buffer[8] = hex_digits[(sn_id3 >> 4) & 0xF];
    p_buffer[9] = hex_digits[sn_id3 & 0xF];
    p_buffer[10] = '\0';

    return SUCCESS;
}

/* ---------- INITIALIZATION ---------- */

/**
 * \fn gyro_reset
 *
 * \brief Resets the sensor by pulsing its active-low EXTRESN pin
 */
void gyro_reset(void) {
    GYRO_RST_LOW();
    gyro_delay_ms(SCH16T_RESET_PULSE_MS);
    GYRO_RST_HIGH();
}

/**
 * \fn gyro_configure
 *
 * \brief Writes the filter, sensitivity, decimation and DRY settings, then starts measuring
 *
 * \returns `status_t`, whether every register was written and read back intact
 */
static status_t gyro_configure(void) {
    ret_err_status(gyro_set_filters(gyro_filter.Rate12, gyro_filter.Acc12, gyro_filter.Acc3), "gyro: could not set filters\n");
    ret_err_status(gyro_set_rate_sens_dec(gyro_sensitivity.Rate1, gyro_sensitivity.Rate2, gyro_decimation.Rate2),
                   "gyro: could not set rate sensitivity and decimation\n");
    ret_err_status(gyro_set_acc_sens_dec(gyro_sensitivity.Acc1, gyro_sensitivity.Acc2, gyro_sensitivity.Acc3, gyro_decimation.Acc2),
                   "gyro: could not set acceleration sensitivity and decimation\n");
    ret_err_status(gyro_set_dry(false, SCH16T_ENABLE_DRY), "gyro: could not configure the DRY pin\n");

    // Write EN_SENSOR = 1, leaving EOI for after the sensor has settled
    ret_err_status(gyro_enable_meas(true, false), "gyro: could not enter measurement mode\n");

    return SUCCESS;
}

/**
 * \fn init_gyro_hardware
 *
 * \brief Initializes the gyro hardware
 *
 * \returns `status_t` SUCCESS if the sensor came up healthy, `ERROR_NOT_READY` if it still
 *          reported a fault after `SCH16T_STARTUP_ATTEMPTS` attempts
 *
 * \note This runs the startup sequence from section 5 ("Component Operation, Reset and Power
 *       Up") of the SCH16T data sheet. Failures within an attempt are logged but not returned,
 *       so that a transient fault gets a second attempt rather than failing the whole init.
 */
status_t init_gyro_hardware(void) {
    spi_m_sync_enable(&SPI_MAGNETOMETER_GYRO); // if you forget this line, every transfer returns -20

    GYRO_CS_HIGH(); // leave the gyro deselected until a transfer wants it
    gyro_reset();

    status_t status = ERROR_NOT_READY;

    for (uint8_t attempt = 0; attempt < SCH16T_STARTUP_ATTEMPTS; attempt++) {
        // Wait for the sensor to read its non-volatile memory
        gyro_delay_ms(SCH16T_NVM_READ_WAIT_MS);

        if (gyro_configure() != SUCCESS) {
            warning("gyro hardware init: could not configure sensor on attempt %d\n", attempt);
        }

        // Wait for the sensor to stabilise now that it is measuring
        gyro_delay_ms(SCH16T_STARTUP_WAIT_MS);

        // The data sheet asks for one status read here. Its contents are not checked: the
        // sensor is still latching start-up faults that only settle once EOI has been set.
        SCH1_status_t device_status;
        if (gyro_get_status(&device_status) != SUCCESS) {
            warning("gyro hardware init: could not read status registers on attempt %d\n", attempt);
        }

        if (gyro_enable_meas(true, true) != SUCCESS) {
            warning("gyro hardware init: could not set the end-of-initialization bit on attempt %d\n", attempt);
        }

        gyro_delay_ms(SCH16T_EOI_WAIT_MS);

        // Read the status registers twice; only the second read reflects the post-EOI state
        status_t first_read = gyro_get_status(&device_status);
        status_t second_read = gyro_get_status(&device_status);

        if (first_read == SUCCESS && second_read == SUCCESS && gyro_status_ok(&device_status)) {
            status = SUCCESS;
            break;
        }

        warning("gyro hardware init: sensor reported a fault on attempt %d; resetting\n", attempt);
        gyro_reset();
    }

    if (status != SUCCESS) {
        warning("gyro hardware init: sensor failed to initialize\n");
        return status;
    }

    info("gyro hardware init: SCH16T initialized\n");
    return SUCCESS;
}
