#!/usr/bin/env python3
"""Host regressions for the actual Arducam driver, without firmware or hardware.

Run: python3 scripts/test_arducam_driver.py -v
Requires a host C compiler (CC, or cc/gcc/clang). Like test_camera_task.py,
all generated headers, harnesses and binaries live in a TemporaryDirectory;
each scenario runs in a fresh process. Production driver, globals and sensor
register tables remain in use; only platform services are mocked.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import struct
import zlib
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

STUB_HEADERS = {
    "FreeRTOS.h": r"""
#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef struct { unsigned int unused; } StaticTask_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif
""",
    "queue.h": "typedef void *QueueHandle_t;\n",
    "task.h": r"""
#ifndef HOST_TASK_H
#define HOST_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
#endif
""",
    "atmel_start.h": r"""
#ifndef HOST_ATMEL_START_H
#define HOST_ATMEL_START_H
#include <stdint.h>
#define ERR_NONE 0
#define ERR_IO (-5)
#define ERR_INVALID_ARG (-2)
#define CAMERA_CS 17
#define GPIO_DIRECTION_OUT 1
#define I2C_M_SEVEN 0
struct io_descriptor { int token; };
struct i2c_m_sync_desc { int token; };
struct spi_m_sync_descriptor { int token; };
struct spi_xfer { uint8_t *txbuf; uint8_t *rxbuf; uint32_t size; };
extern struct i2c_m_sync_desc I2C_CAMERA;
extern struct spi_m_sync_descriptor SPI_CAMERA;
int32_t io_write(struct io_descriptor *const io, const uint8_t *const data, const uint16_t size);
int32_t io_read(struct io_descriptor *const io, uint8_t *const data, const uint16_t size);
int32_t i2c_m_sync_get_io_descriptor(struct i2c_m_sync_desc *dev, struct io_descriptor **io);
int32_t i2c_m_sync_enable(struct i2c_m_sync_desc *dev);
int32_t i2c_m_sync_set_slaveaddr(struct i2c_m_sync_desc *dev, uint16_t address, int mode);
void spi_m_sync_disable(struct spi_m_sync_descriptor *dev);
int32_t spi_m_sync_set_baudrate(struct spi_m_sync_descriptor *dev, uint32_t baud);
int32_t spi_m_sync_get_io_descriptor(struct spi_m_sync_descriptor *dev, struct io_descriptor **io);
void spi_m_sync_enable(struct spi_m_sync_descriptor *dev);
int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *dev, const struct spi_xfer *xfer);
void gpio_set_pin_direction(uint32_t pin, int direction);
void gpio_set_pin_level(uint32_t pin, int level);
#endif
""",
    "driver_init.h": '#include "atmel_start.h"\n',
    "hpl_sercom_config.h": "#define CONF_GCLK_SERCOM2_CORE_FREQUENCY 48000000U\n",
    "rtos_start.h": r"""
#include "task.h"
#include "logging.h"
#include "SEGGER_RTT.h"
""",
    "logging.h": r"""
void warning(const char *format, ...);
void info(const char *format, ...);
#define ret_err_status(expr, message) do { \
    status_t status = (expr); \
    if (status != SUCCESS) { warning(message); return status; } \
} while (0)
""",
    "SEGGER_RTT.h": r"""
/* The task configures NO_BLOCK_SKIP; Write accepts a whole chunk or zero. */
#define SEGGER_RTT_MODE_NO_BLOCK_SKIP 0
unsigned int SEGGER_RTT_Write(unsigned int channel, const void *buffer, unsigned int size);
""",
    "mutexes.h": r"""
extern void *i2c_camera_bus_mutex;
void lock_mutex(void *mutex);
void unlock_mutex(void *mutex);
""",
    "task_list.h": r"""
#include "globals.h"
pvdx_task_t *get_current_task(void);
""",
    "watchdog_task.h": r"""
#include "globals.h"
bool should_checkin(pvdx_task_t *task);
void watchdog_checkin(pvdx_task_t *task);
""",
}

HARNESS = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arducam_driver.h"
#include "arducam_registers.h"
#include "SEGGER_RTT.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

struct i2c_m_sync_desc I2C_CAMERA;
struct spi_m_sync_descriptor SPI_CAMERA;
static struct io_descriptor spi_io, i2c_io;
static int mutex_token;
void *i2c_camera_bus_mutex = &mutex_token;
static pvdx_task_t camera_task;
static int watchdog_mode = 2; /* 0: no task, 1: checkin not due, 2: due */
static unsigned int checkins, should_calls, task_calls;
static int cs = 1, mutex_depth;
static unsigned int cs_lows, cs_highs, delays, warnings;
static TickType_t ticks;
static size_t image_size = 9007, fifo_offset, output_size;
static uint8_t output[OV2640_MAX_FIFO_SIZE];
static uint8_t ring[4096], wire[2 * OV2640_MAX_FIFO_SIZE];
static size_t rd, wr, wire_size;
static unsigned abort_packets, abort_attempts, accepted_packets;
static int cancel_mode, stop_after_data;
static TickType_t reset_tick;
static int sensor_reset;
static uint8_t pending[4095];
static size_t pending_size;
static uint8_t spi_register, i2c_register, sensor_bank;
static uint8_t sensor_registers[2][256], sensor_written[2][256];
static int burst, never_done, no_host, retry, stream_budget, timed_spi;
static unsigned int spi_byte_remainder;
static TickType_t spi_ticks;
static unsigned int commands, trigger_reads, fifo_reads, transfers, rtt_calls;
static unsigned int begin_markers, end_markers, i2c_writes, i2c_reads;
static unsigned int rejected, chunk_rejections;
static const char *fail_point = "";
static unsigned int fail_nth = 1, fail_seen;
static int32_t fail_result = ERR_IO;
#define STREAM_CHUNK_SIZE 4067U
static size_t transfer_sizes[(OV2640_MAX_FIFO_SIZE + STREAM_CHUNK_SIZE - 1U) / STREAM_CHUNK_SIZE];

/* Failure injection is counted only at the selected platform operation. */
static int fails(const char *point) {
    if (strcmp(point, fail_point) != 0) return 0;
    return ++fail_seen == fail_nth;
}

static uint8_t image_byte(size_t offset) {
    /* A nontrivial pattern catches missing first bytes, repeats and reordering. */
    if (offset == 0) return 0xff;
    if (offset == 1) return 0xd8;
    return (uint8_t)((offset * 37U) ^ (offset >> 7) ^ (offset >> 13));
}

void gpio_set_pin_direction(uint32_t pin, int direction) {
    CHECK(pin == CAMERA_CS && direction == GPIO_DIRECTION_OUT);
}

void gpio_set_pin_level(uint32_t pin, int level) {
    CHECK(pin == CAMERA_CS);
    if (level) {
        ++cs_highs;
        burst = 0;
    } else {
        CHECK(cs == 1); /* Never open a second transaction inside a burst. */
        ++cs_lows;
    }
    cs = level;
}

int32_t io_write(struct io_descriptor *const io, const uint8_t *const data, const uint16_t size) {
    CHECK(data != NULL);
    if (io == &i2c_io) {
        CHECK(mutex_depth == 1 && (size == 1 || size == 2));
        ++i2c_writes;
        if (sensor_reset) CHECK((TickType_t)(ticks - reset_tick) >= 100);
        if (fails("i2c_write")) return fail_result;
        i2c_register = data[0];
        if (size == 2) {
            if (data[0] == 0xff) {
                CHECK(data[1] <= 1);
                sensor_bank = data[1];
            } else {
                if (sensor_bank == 1 && data[0] == 0x12 && (data[1] & 0x80)) {
                    sensor_reset = 1;
                    reset_tick = ticks;
                    memset(sensor_registers, 0, sizeof(sensor_registers));
                    memset(sensor_written, 0, sizeof(sensor_written));
                }
                sensor_registers[sensor_bank][data[0]] = data[1];
                sensor_written[sensor_bank][data[0]] = 1;
            }
        }
        return size;
    }
    CHECK(io == &spi_io && cs == 0 && !burst);
    if (size == 2) {
        CHECK(data[0] == (ARDUCHIP_FIFO | 0x80) || data[0] == (ARDUCHIP_TEST1 | 0x80));
        if (data[0] == (ARDUCHIP_FIFO | 0x80)) {
            ++commands;
            CHECK(commands <= 3);
            CHECK(data[1] == (commands == 3 ? FIFO_START_MASK : FIFO_CLEAR_MASK));
            if (fails("command")) return fail_result;
        } else CHECK(data[1] == 0x55);
        return size;
    }
    CHECK(size == 1);
    spi_register = data[0];
    if (data[0] == BURST_FIFO_READ) {
        CHECK(commands == 3 && trigger_reads > 0 && fifo_reads == 3);
        if (fails("burst")) return fail_result;
        burst = 1;
    } else {
        const char *point = data[0] == ARDUCHIP_TRIG ? "trigger_command" :
                            data[0] >= FIFO_SIZE1 && data[0] <= FIFO_SIZE3 ? "fifo_command" : "read_command";
        if (fails(point)) return fail_result;
    }
    return size;
}

int32_t io_read(struct io_descriptor *const io, uint8_t *const data, const uint16_t size) {
    CHECK(data != NULL && size == 1);
    if (io == &i2c_io) {
        CHECK(mutex_depth == 1);
        ++i2c_reads;
        if (fails("i2c_read")) return fail_result;
        CHECK(i2c_register == OV2640_CHIPID_HIGH || i2c_register == OV2640_CHIPID_LOW);
        data[0] = i2c_register == OV2640_CHIPID_HIGH ? 0x26 : 0x42;
        return size;
    }
    CHECK(io == &spi_io && cs == 0 && !burst);
    const char *point;
    if (spi_register == ARDUCHIP_TRIG) {
        ++trigger_reads;
        point = "trigger_read";
        data[0] = never_done ? 0 : CAP_DONE_MASK;
    } else if (spi_register >= FIFO_SIZE1 && spi_register <= FIFO_SIZE3) {
        ++fifo_reads;
        CHECK(spi_register == FIFO_SIZE1 + fifo_reads - 1);
        point = "fifo_read";
        data[0] = (uint8_t)(image_size >> (8 * (spi_register - FIFO_SIZE1)));
    } else {
        CHECK(spi_register == ARDUCHIP_TEST1);
        point = "read_data";
        data[0] = 0x55;
    }
    if (fails(point)) return fail_result;
    return size;
}

int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *dev, const struct spi_xfer *xfer) {
    CHECK(dev == &SPI_CAMERA && cs == 0 && burst);
    CHECK(xfer->txbuf == NULL && xfer->rxbuf == ardu_spi_rx_buffer + 28);
    CHECK(xfer->size > 0 && xfer->size <= 4067);
    CHECK(fifo_offset == output_size); /* An unaccepted RTT chunk must be retried, not reread. */
    CHECK(fifo_offset + xfer->size <= image_size);
    CHECK(transfers < sizeof(transfer_sizes) / sizeof(transfer_sizes[0]));
    CHECK(checkins == (watchdog_mode == 2 ? transfers : 0U));
    transfer_sizes[transfers++] = xfer->size;
    if (fails("transfer")) {
        if (fail_result == -100) return (int32_t)xfer->size - 1;
        if (fail_result == -101) return (int32_t)xfer->size + 1;
        return fail_result;
    }
    for (size_t i = 0; i < xfer->size; ++i) xfer->rxbuf[i] = image_byte(fifo_offset + i);
    fifo_offset += xfer->size;
    if (timed_spi) {
        /* 1 MHz / 8 = 125000 bytes/s; carry fractional milliseconds across chunks. */
        spi_byte_remainder += xfer->size;
        TickType_t elapsed = spi_byte_remainder / 125U;
        spi_byte_remainder %= 125U;
        ticks += elapsed;
        spi_ticks += elapsed;
    }
    chunk_rejections = 0;
    return xfer->size; /* ASF returns byte count, not ERR_NONE on success. */
}

static void host_drain(size_t budget) {
    while (rd != wr && budget--) {
        CHECK(wire_size < sizeof(wire));
        wire[wire_size++] = ring[rd];
        rd = (rd + 1) % sizeof(ring);
    }
}

unsigned int SEGGER_RTT_Write(unsigned int channel, const void *buffer, unsigned int size) {
    CHECK(channel == CAMERA_RTT_OUTPUT_CHANNEL && channel == 2);
    CHECK(buffer == ardu_spi_rx_buffer && size >= 28 && size <= 4095);
    const uint8_t *bytes = buffer;
    CHECK(memcmp(bytes, "PVIM", 4) == 0 && bytes[4] == 1);
    unsigned type = bytes[5];
    unsigned payload_size = bytes[6] | ((unsigned)bytes[7] << 8);
    CHECK(size == 28 + payload_size);
    if (type != 4 && pending_size) {
        CHECK(size == pending_size && memcmp(pending, bytes, size) == 0);
    }
    ++rtt_calls;
    if (type == 4) {
        ++abort_attempts;
        CHECK(cs == 1 && payload_size == 4);
        uint32_t offset = (uint32_t)bytes[12] | ((uint32_t)bytes[13] << 8) |
                          ((uint32_t)bytes[14] << 16) | ((uint32_t)bytes[15] << 24);
        uint32_t status = (uint32_t)bytes[28] | ((uint32_t)bytes[29] << 8) |
                          ((uint32_t)bytes[30] << 16) | ((uint32_t)bytes[31] << 24);
        CHECK(offset == output_size);
        CHECK(status == (uint32_t)(cancel_mode ? ERROR_TASK_DISABLED :
                                  strcmp(fail_point, "transfer") == 0 || strcmp(fail_point, "burst") == 0 ?
                                  ERROR_SPI_TRANSFER_FAILED : ERROR_WRITE_FAILED));
    }
    size_t free_bytes = (rd + sizeof(ring) - wr - 1) % sizeof(ring);
    if (size > free_bytes || (type != 4 && ((retry && chunk_rejections < 3) ||
                                          (stream_budget && chunk_rejections < 600)))) {
        if (type != 4) {
            memcpy(pending, bytes, size);
            pending_size = size;
            ++chunk_rejections;
        }
        ++rejected;
        return 0; /* Actual bounded NO_BLOCK_SKIP ring: never a partial packet. */
    }
    for (size_t i = 0; i < size; ++i) {
        ring[wr] = bytes[i];
        wr = (wr + 1) % sizeof(ring);
    }
    if (type == 1) {
        CHECK(cs == 1 && payload_size == 4);
        ++begin_markers;
    } else if (type == 2) {
        CHECK(cs == 0 && burst && fifo_offset == output_size + payload_size);
        CHECK(output_size + payload_size <= sizeof(output));
        for (size_t i = 0; i < payload_size; ++i) CHECK(bytes[28 + i] == image_byte(output_size + i));
        memcpy(output + output_size, bytes + 28, payload_size);
        output_size += payload_size;
        if (stop_after_data) no_host = 1;
    } else if (type == 3) {
        CHECK(cs == 1 && output_size == image_size);
        ++end_markers;
    } else {
        CHECK(type == 4 && cs == 1);
        ++abort_packets;
    }
    ++accepted_packets;
    pending_size = 0;
    chunk_rejections = 0;
    return size;
}

TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t count) {
    CHECK(count > 0);
    ticks += count;
    if (!no_host) host_drain(1024);
    ++delays;
    CHECK(delays <= 5000); /* Fail fast if either driver timeout stops working. */
}

void warning(const char *format, ...) { CHECK(format != NULL); ++warnings; }
void info(const char *format, ...) { CHECK(format != NULL); }

static bool cancelled(void) {
    if (cancel_mode == 1) return ticks >= 3;
    if (cancel_mode == 2) return rejected >= 3;
    if (cancel_mode == 3) return checkins >= 1;
    if (cancel_mode == 4) return fifo_offset > 0;
    if (cancel_mode == 5) return cs == 1 && fifo_offset == image_size && rejected >= 4;
    if (cancel_mode == 6) return rejected >= 3;
    if (cancel_mode == 7) return true;
    return false;
}

pvdx_task_t *get_current_task(void) { ++task_calls; return watchdog_mode ? &camera_task : NULL; }
bool should_checkin(pvdx_task_t *task) {
    CHECK(task == &camera_task);
    ++should_calls;
    return watchdog_mode == 2;
}
void watchdog_checkin(pvdx_task_t *task) { CHECK(task == &camera_task); ++checkins; }

void lock_mutex(void *mutex) { CHECK(mutex == i2c_camera_bus_mutex && mutex_depth == 0); ++mutex_depth; }
void unlock_mutex(void *mutex) { CHECK(mutex == i2c_camera_bus_mutex && mutex_depth == 1); --mutex_depth; }
int32_t i2c_m_sync_get_io_descriptor(struct i2c_m_sync_desc *dev, struct io_descriptor **io) {
    CHECK(dev == &I2C_CAMERA && mutex_depth == 1);
    if (fails("i2c_get")) return fail_result;
    *io = &i2c_io;
    return ERR_NONE;
}
int32_t i2c_m_sync_enable(struct i2c_m_sync_desc *dev) {
    CHECK(dev == &I2C_CAMERA && mutex_depth == 1);
    return fails("i2c_enable") ? fail_result : ERR_NONE;
}
int32_t i2c_m_sync_set_slaveaddr(struct i2c_m_sync_desc *dev, uint16_t address, int mode) {
    CHECK(dev == &I2C_CAMERA && mutex_depth == 1 && address == (ARDUCAM_ADDR >> 1) && mode == I2C_M_SEVEN);
    return ERR_NONE;
}
void spi_m_sync_disable(struct spi_m_sync_descriptor *dev) { CHECK(dev == &SPI_CAMERA); }
void spi_m_sync_enable(struct spi_m_sync_descriptor *dev) { CHECK(dev == &SPI_CAMERA); }
int32_t spi_m_sync_set_baudrate(struct spi_m_sync_descriptor *dev, uint32_t baud) {
    CHECK(dev == &SPI_CAMERA && baud == 23); /* 48 MHz / (2 * 1 MHz) - 1 */
    return ERR_NONE;
}
int32_t spi_m_sync_get_io_descriptor(struct spi_m_sync_descriptor *dev, struct io_descriptor **io) {
    CHECK(dev == &SPI_CAMERA);
    *io = &spi_io;
    return ERR_NONE;
}

static void released(void) {
    CHECK(cs == 1 && mutex_depth == 0);
    CHECK(cs_highs >= cs_lows);
}

static void successful_stream(void) {
    const TickType_t start = ticks;
    const size_t expected_chunks = (image_size + STREAM_CHUNK_SIZE - 1U) / STREAM_CHUNK_SIZE;
    const unsigned int minimum_rejected = (expected_chunks + 2) * (stream_budget ? 600U : retry ? 3U : 0U);
    CHECK(capture_rtt() == SUCCESS);
    released();
    CHECK(commands == 3 && trigger_reads == 1 && fifo_reads == 3);
    CHECK(begin_markers == 1 && end_markers == 1);
    CHECK(fifo_offset == image_size && output_size == image_size);
    CHECK(transfers == expected_chunks);
    for (size_t i = 0, remaining = image_size; i < transfers; ++i) {
        size_t expected = remaining < STREAM_CHUNK_SIZE ? remaining : STREAM_CHUNK_SIZE;
        CHECK(transfer_sizes[i] == expected && expected > 0 && expected <= 4095);
        remaining -= expected;
    }
    for (size_t i = 0; i < image_size; ++i) CHECK(output[i] == image_byte(i));
    CHECK(task_calls == expected_chunks);
    CHECK(should_calls == (watchdog_mode ? expected_chunks : 0U));
    CHECK(checkins == (watchdog_mode == 2 ? expected_chunks : 0U));
    CHECK(rtt_calls == expected_chunks + 2 + rejected);
    CHECK(rejected >= minimum_rejected);
    CHECK(delays == rejected && (TickType_t)(ticks - start) == rejected + spi_ticks && warnings == 0);
}

int main(int argc, char **argv) {
    CHECK(argc >= 2);
    arducam_spi_io = &spi_io;
    arducam_i2c_io = &i2c_io;
    CHECK(SEGGER_RTT_IMAGE_BUFFER_SIZE == 4096 && ARDUCAM_RTT_TIMEOUT_MS == 1000);
    if (argc > 2) fail_point = argv[2];
    if (argc > 3) fail_nth = (unsigned int)strtoul(argv[3], NULL, 10);
    if (argc > 4) fail_result = (int32_t)strtol(argv[4], NULL, 10);
    if (argc > 3 && strcmp(argv[3], "last") == 0) {
        const struct sensor_reg *tables[] = {OV2640_JPEG_INIT, OV2640_YUV422, OV2640_JPEG, OV2640_1600x1200_JPEG};
        fail_nth = 7; /* ID bank/address writes, reset pair, bank select and 0x15. */
        for (size_t table = 0; table < 4; ++table) {
            for (const struct sensor_reg *r = tables[table]; r->reg != 0xff || r->val != 0xff; ++r) ++fail_nth;
        }
    }

    if (strcmp(argv[1], "success") == 0 || strcmp(argv[1], "retry") == 0) {
        retry = strcmp(argv[1], "retry") == 0;
        if (argc > 2) watchdog_mode = atoi(argv[2]);
        successful_stream();
    } else if (strcmp(argv[1], "spi_failure") == 0) {
        CHECK(capture_rtt() == ERROR_SPI_TRANSFER_FAILED);
        CHECK(fail_seen == fail_nth);
        CHECK(end_markers == 0);
        if (strcmp(fail_point, "transfer") == 0) {
            CHECK(transfers == fail_nth && abort_attempts == 1);
            CHECK(abort_packets == (fail_nth == 1 ? 1U : 0U));
            CHECK(output_size == (fail_nth - 1) * STREAM_CHUNK_SIZE);
        } else {
            CHECK(transfers == 0);
            CHECK(begin_markers == (strcmp(fail_point, "burst") == 0 ? 1U : 0U));
            CHECK(abort_packets == begin_markers);
        }
        released();
    } else if (strcmp(argv[1], "checked_read") == 0) {
        uint8_t value = 0;
        status_t expected = *fail_point ? ERROR_SPI_TRANSFER_FAILED : SUCCESS;
        CHECK(arducam_spi_read(ARDUCHIP_TEST1 | 0x80, &value) == expected);
        if (expected == SUCCESS) CHECK(value == 0x55);
        else CHECK(fail_seen == fail_nth);
        CHECK(cs_lows == 1 && cs_highs == 1);
        released();
    } else if (strcmp(argv[1], "null_read") == 0) {
        CHECK(arducam_spi_read(ARDUCHIP_TEST1, NULL) == ERROR_BAD_ARGS);
        CHECK(cs_lows == 0 && cs_highs == 0);
    } else if (strcmp(argv[1], "stream_budget") == 0) {
        stream_budget = 1;
        TickType_t start = argc > 2 ? UINT32_MAX - 300U : 0;
        ticks = start;
        successful_stream();
        CHECK((TickType_t)(ticks - start) == 3000 && checkins == 3);
    } else if (strcmp(argv[1], "large_slow_spi") == 0) {
        image_size = OV2640_MAX_FIFO_SIZE - 1U;
        timed_spi = 1;
        retry = 1; /* Exercise timeout checks even after SPI alone has exceeded one second. */
        successful_stream();
        CHECK(spi_ticks == image_size / 125U && spi_ticks > ARDUCAM_RTT_TIMEOUT_MS);
        CHECK(spi_byte_remainder == image_size % 125U);
        CHECK(checkins == transfers && transfers > 16);
    } else if (strcmp(argv[1], "no_host") == 0) {
        no_host = 1;
        CHECK(capture_rtt() == ERROR_WRITE_FAILED);
        CHECK(ticks == 1000 && delays == 1000 && rejected == 1001);
        CHECK(transfers == 1);
        CHECK(output_size == 0);
        CHECK(begin_markers == 1 && end_markers == 0 && abort_packets == 1 && warnings > 0);
        released();
    } else if (strcmp(argv[1], "cancel") == 0) {
        cancel_mode = atoi(argv[2]);
        never_done = cancel_mode == 1;
        no_host = cancel_mode == 2;
        if (cancel_mode == 5) {
            image_size = 4067;
            stop_after_data = 1;
        }
        retry = cancel_mode == 6;
        CHECK(capture_rtt_cancellable(cancelled) == ERROR_TASK_DISABLED);
        CHECK(end_markers == 0);
        CHECK(abort_attempts == (cancel_mode == 1 || cancel_mode == 6 ? 0U : 1U));
        CHECK(abort_packets == (cancel_mode == 1 || cancel_mode == 3 || cancel_mode == 5 || cancel_mode == 6 ? 0U : 1U));
        if (cancel_mode == 1) CHECK(ticks == 3 && trigger_reads == 3 && transfers == 0);
        if (cancel_mode == 2) CHECK(ticks == 3 && transfers == 1);
        if (cancel_mode == 3) CHECK(output_size == 4067 && transfers == 1);
        if (cancel_mode == 4) CHECK(output_size == 0 && transfers == 1);
        if (cancel_mode == 5) CHECK(output_size == image_size && transfers == 1 && ticks == 4);
        if (cancel_mode == 6) CHECK(begin_markers == 0 && transfers == 0 && ticks == 3);
        released();
    } else if (strcmp(argv[1], "end_timeout") == 0) {
        image_size = 4067;
        stop_after_data = 1;
        CHECK(capture_rtt() == ERROR_WRITE_FAILED);
        CHECK(ticks == 1001 && output_size == image_size && end_markers == 0);
        CHECK(abort_attempts == 1 && abort_packets == 0);
        released();
    } else if (strcmp(argv[1], "attempt_ids") == 0) {
        cancel_mode = 7;
        CHECK(capture_rtt_cancellable(cancelled) == ERROR_TASK_DISABLED);
        CHECK(commands == 0 && rtt_calls == 0);
        cancel_mode = 0;
        image_size = 0;
        CHECK(capture_rtt() == ERROR_SANITY_CHECK_FAILED);
        CHECK(rtt_calls == 0);
        commands = trigger_reads = fifo_reads = 0;
        image_size = 9007;
        successful_stream();
    } else if (strcmp(argv[1], "stale_retry") == 0) {
        if (argc > 2) {
            cancel_mode = 3;
            CHECK(capture_rtt_cancellable(cancelled) == ERROR_TASK_DISABLED);
            CHECK(abort_attempts == 1 && abort_packets == 0 && output_size == 4067);
        } else {
            no_host = 1;
            CHECK(capture_rtt() == ERROR_WRITE_FAILED);
            CHECK(begin_markers == 1 && abort_packets == 1 && rd == 0 && wr == 64);
        }
        released();
        /* Leave the unread stale prefix in the real ring; the driver must not reset it. */
        no_host = cancel_mode = 0;
        commands = trigger_reads = fifo_reads = transfers = rtt_calls = 0;
        begin_markers = end_markers = abort_packets = abort_attempts = accepted_packets = 0;
        rejected = chunk_rejections = checkins = should_calls = task_calls = 0;
        fifo_offset = output_size = pending_size = 0;
        delays = warnings = 0;
        retry = 1;
        successful_stream();
    } else if (strcmp(argv[1], "capture_timeout") == 0) {
        never_done = 1;
        TickType_t start = argc > 2 ? UINT32_MAX - 10U : 0;
        ticks = start;
        CHECK(capture_rtt() == ERROR_NOT_READY);
        CHECK((TickType_t)(ticks - start) == ARDUCAM_CAPTURE_TIMEOUT_MS);
        CHECK(delays == ARDUCAM_CAPTURE_TIMEOUT_MS && trigger_reads == delays + 1);
        CHECK(fifo_reads == 0 && transfers == 0 && rtt_calls == 0);
        CHECK(begin_markers == 0 && end_markers == 0 && warnings > 0);
        released();
    } else if (strcmp(argv[1], "invalid_length") == 0) {
        image_size = (size_t)strtoul(argv[2], NULL, 10);
        CHECK(capture_rtt() == ERROR_SANITY_CHECK_FAILED);
        CHECK(fifo_reads == 3 && transfers == 0 && rtt_calls == 0);
        CHECK(begin_markers == 0 && end_markers == 0);
        released();
    } else if (strcmp(argv[1], "init_success") == 0) {
        CHECK(init_arducam_hardware() == SUCCESS);
        CHECK(i2c_writes > 100 && i2c_reads == 2 && ticks == 100);
        CHECK(sensor_written[1][0x12]);
        CHECK((sensor_registers[1][0x12] & 0x70) == 0); /* COM7: UXGA sensor mode. */
        CHECK(sensor_written[0][0x5a] && sensor_written[0][0x5b] && sensor_written[0][0x5c]);
        /* DSP ZMOW/ZMOH/ZMHH encode output dimensions in units of four pixels. */
        unsigned int width = (sensor_registers[0][0x5a] | ((sensor_registers[0][0x5c] & 3U) << 8)) * 4U;
        unsigned int height = (sensor_registers[0][0x5b] | ((sensor_registers[0][0x5c] & 4U) << 6)) * 4U;
        CHECK(width == 1600 && height == 1200);
        released();
    } else if (strcmp(argv[1], "init_failure") == 0) {
        CHECK(init_arducam_hardware() == ERROR_I2C_FAILED);
        CHECK(fail_seen == fail_nth);
        CHECK(transfers == 0 && rtt_calls == 0);
        released();
    } else CHECK(0);
    host_drain(sizeof(ring));
    printf("WIRE ");
    for (size_t i = 0; i < wire_size; ++i) printf("%02x", wire[i]);
    printf("\nPASS %s\n", argv[1]);
    return EXIT_SUCCESS;
}
"""


class ArducamDriverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CC", ""))
        if not compiler:
            compiler = next(([cc] for cc in ("cc", "gcc", "clang") if shutil.which(cc)), [])
        if not compiler or not shutil.which(compiler[0]):
            raise RuntimeError("A host C compiler is required; install cc/gcc/clang or set CC")

        temporary = tempfile.TemporaryDirectory(prefix="arducam-driver-test-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        for name, contents in STUB_HEADERS.items():
            (directory / name).write_text(contents, encoding="utf-8")
        harness = directory / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        cls.binary = directory / "arducam_driver_test"
        command = compiler + [
            "-std=c99", "-Wall", "-Wextra", "-Werror", "-DUNITTEST",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-I", str(directory),
            "-I", str(ROOT / "src"),
            "-I", str(ROOT / "src/drivers/arducam"),
            str(ROOT / "src/drivers/arducam/arducam_driver.c"),
            str(ROOT / "src/drivers/arducam/arducam_registers.c"),
            str(harness), "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(
                f"Host compilation failed: {shlex.join(command)}\n{result.stdout}{result.stderr}"
            )

    def run_case(self, case, *arguments):
        command = [str(self.binary), case, *(str(arg) for arg in arguments)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, msg=f"{shlex.join(command)}:\n{result.stdout}{result.stderr}")
        wire_line, pass_line = result.stdout.splitlines()
        self.assertEqual(pass_line, f"PASS {case}")
        self.assertTrue(wire_line.startswith("WIRE "))
        wire = bytes.fromhex(wire_line[5:])
        frames = {}
        cursor = 0
        last_id = 0
        while cursor < len(wire):
            header = wire[cursor:cursor + 28]
            self.assertEqual(len(header), 28)
            magic, version, kind, size, frame_id, offset, total, crc, header_crc = struct.unpack("<4sBBHIIIII", header)
            self.assertEqual((magic, version), (b"PVIM", 1))
            self.assertEqual(zlib.crc32(header[:24]), header_crc)
            self.assertLessEqual(size, 4067)
            payload = wire[cursor + 28:cursor + 28 + size]
            self.assertEqual(len(payload), size)
            self.assertEqual(zlib.crc32(payload), crc)
            cursor += 28 + size
            if kind == 1:
                self.assertGreater(frame_id, last_id)
                last_id = frame_id
                self.assertEqual((offset, payload), (0, struct.pack("<HH", 1600, 1200)))
                frames[frame_id] = {"total": total, "data": bytearray(), "terminal": None}
            else:
                self.assertIn(frame_id, frames)
                frame = frames[frame_id]
                self.assertIsNone(frame["terminal"])
                self.assertEqual(total, frame["total"])
                self.assertEqual(offset, len(frame["data"]))
                if kind == 2:
                    self.assertGreater(size, 0)
                    frame["data"].extend(payload)
                    self.assertLessEqual(len(frame["data"]), total)
                elif kind == 3:
                    self.assertEqual(size, 4)
                    self.assertEqual(offset, total)
                    self.assertEqual(struct.unpack("<I", payload)[0], zlib.crc32(frame["data"]))
                    frame["terminal"] = kind
                else:
                    self.assertEqual(kind, 4)
                    self.assertEqual(size, 4)
                    self.assertNotEqual(struct.unpack("<I", payload)[0], 0)
                    frame["terminal"] = kind
        if case == "attempt_ids":
            self.assertEqual(list(frames), [3])
        if case == "stale_retry":
            self.assertEqual(list(frames), [1, 2])
            self.assertEqual([frame["terminal"] for frame in frames.values()], [None if arguments else 4, 3])

    def test_multichunk_stream_preserves_every_byte_and_releases_cs(self):
        for watchdog_mode in (0, 1, 2):
            with self.subTest(watchdog_mode=watchdog_mode):
                self.run_case("success", watchdog_mode)

    def test_rtt_retries_intact_chunks_without_rereading_fifo(self):
        self.run_case("retry")

    def test_spi_capture_commands_require_exact_byte_count(self):
        for nth in (1, 2, 3):
            for result in (-5, 0, 1, 3):
                with self.subTest(command=nth, result=result):
                    self.run_case("spi_failure", "command", nth, result)

    def test_trigger_and_fifo_register_errors_propagate(self):
        for point, occurrences in (("trigger_command", (1,)), ("trigger_read", (1,)),
                                   ("fifo_command", (1, 2, 3)), ("fifo_read", (1, 2, 3))):
            for nth in occurrences:
                for result in (-5, 0, 2):
                    with self.subTest(point=point, nth=nth, result=result):
                        self.run_case("spi_failure", point, nth, result)

    def test_burst_command_errors_release_cs_without_reading_fifo(self):
        for result in (-5, 0, 2):
            with self.subTest(result=result):
                self.run_case("spi_failure", "burst", 1, result)

    def test_spi_transfer_requires_exact_byte_count_not_err_none(self):
        # -100/-101 tell the harness to return requested count minus/plus one.
        for nth in (1, 2, 3):
            for result in (-5, 0, -100, -101):
                with self.subTest(chunk=nth, result=result):
                    self.run_case("spi_failure", "transfer", nth, result)

    def test_checked_spi_read_success_and_failures_release_cs(self):
        self.run_case("checked_read")
        self.run_case("null_read")
        for point in ("read_command", "read_data"):
            for result in (-5, 0, 2):
                with self.subTest(point=point, result=result):
                    self.run_case("checked_read", point, 1, result)

    def test_cancellation_releases_cs_at_poll_wait_and_transfer_boundaries(self):
        for mode in (1, 2, 3, 4, 5, 6):
            with self.subTest(mode=mode):
                self.run_case("cancel", mode)

    def test_new_capture_preserves_and_supersedes_unread_aborted_prefix(self):
        self.run_case("stale_retry")
        self.run_case("stale_retry", "dropped_abort")

    def test_frame_ids_count_cancelled_and_invalid_attempts(self):
        self.run_case("attempt_ids")

    def test_end_packet_has_its_own_bounded_timeout(self):
        self.run_case("end_timeout")

    def test_absent_host_times_out_after_1000ms_without_end_packet(self):
        self.run_case("no_host")

    def test_rtt_timeout_resets_per_chunk_including_tick_wraparound(self):
        self.run_case("stream_budget")
        self.run_case("stream_budget", "wrap")

    def test_near_fifo_limit_stream_survives_slow_spi_with_rtt_progress(self):
        self.run_case("large_slow_spi")

    def test_capture_timeout_including_tick_wraparound(self):
        self.run_case("capture_timeout")
        self.run_case("capture_timeout", "wrap")

    def test_invalid_fifo_lengths_never_start_stream(self):
        for length in (0, 0x5FFFF, 0x60000):
            with self.subTest(length=length):
                self.run_case("invalid_length", length)

    def test_init_programs_uxga_sensor_and_1600x1200_dsp_output(self):
        self.run_case("init_success")

    def test_init_propagates_i2c_setup_failures(self):
        for point in ("i2c_get", "i2c_enable"):
            with self.subTest(point=point):
                self.run_case("init_failure", point, 1, -5)

    def test_init_propagates_i2c_sensor_id_reset_and_table_failures(self):
        # Includes sensor bank/ID address writes, reset, and early/later table entries.
        for nth in (*range(1, 11), 20, 100, "last"):
            for result in (-5, 0):
                with self.subTest(write=nth, result=result):
                    self.run_case("init_failure", "i2c_write", nth, result)
        for nth in (1, 2):
            for result in (-5, 0, 2):
                with self.subTest(read=nth, result=result):
                    self.run_case("init_failure", "i2c_read", nth, result)
        for nth in (1, 4, 5, 6, 100):
            with self.subTest(short_write=nth):
                self.run_case("init_failure", "i2c_write", nth, 1)


if __name__ == "__main__":
    unittest.main()
