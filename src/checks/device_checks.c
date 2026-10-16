/**
 * src/checks/device_checks.c
 *
 * header file for checking the state of hardware device for PVDX
 *
 * Created: 20251102 SUN
 * Authors: Zach Mahan
 */

#include "checks/device_checks.h"

#include <stddef.h>
#include <stdint.h>

#include "driver_init.h"
#include "display_driver.h"
#include "hal_io.h"
#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"

#define MRAM_CHIP_COUNT 3u
#define MRAM_TEST_ADDRESS 0x00000980u
#define MRAM_TEST_SIZE 16u

#define MRAM_CMD_WREN 0x06u
#define MRAM_CMD_RDSR 0x05u
#define MRAM_CMD_READ 0x03u
#define MRAM_CMD_WRITE 0x02u
#define MRAM_CMD_RDID 0x9Fu

#define MRAM_EXPECTED_ID_0 0x6Bu
#define MRAM_EXPECTED_ID_1 0xBBu
#define MRAM_EXPECTED_ID_2 0x14u

// forward declarations of check functions
bool check_magnetometer(void);
bool check_display(void);
bool check_mram(void);
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

static struct io_descriptor *mram_io = NULL;

// ~~~ for safety, these structures should be local to this source file ~~~
typedef struct device_check_state {
    bool checked;
    bool valid;
} device_check_state_t;

static device_check_state_t device_states[NUM_DEVICES] = {0}; // inits to {false, false} for each entry

static bool (*device_check_functions[NUM_DEVICES])(void) = {
    //    func ptr      |  device_id_t
    // -------------------------------
    NULL, // &check_magnetometer, // MAGNETOMETER_ID
    NULL,                // PHOTODIODE_ID
    NULL,                // GYROSCOPE_ID
    &check_mram,         // MRAM_ID
    NULL,                // MAGNETORQUERS_ID
    NULL,                // SBAND_ID
    NULL,                // UHF_ID
    NULL,                // EPS_ID
    NULL,                // DISPLAY_ID — init done inside the display task post-scheduler
    NULL,                // CAMERA_ID
};

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

bool check_all_devices_on_startup(void) {
    bool at_least_one_failed = false;

    info("CHECKING ALL DEVICES...\n");
    info("1 = OKAY / 0 = NOT OKAY...\n");
    for (device_id_t device_id = 0; device_id < NUM_DEVICES; device_id++) {
        bool device_okay = check_device(device_id);
        at_least_one_failed = at_least_one_failed || !device_okay;
        info("%-20s: %d\n", device_name_of(device_id), device_okay);
    }

    return at_least_one_failed;
}

bool check_device(device_id_t device_id) {
    // guard against invalid device_id given
    if (device_id >= NUM_DEVICES) {
        warning("Invalid device_id_t passed to `check_devices`\n");
        return false;
    }
    // guard against calling a null fn
    if (device_check_functions[device_id] == NULL) {
        warning("Check function for device_id %s is NULL\n", device_name_of(device_id));
        return false;
    }
    // only run the check function if "checked" is false for a device
    if (!device_states[device_id].checked) {
        device_states[device_id].valid = device_check_functions[device_id]();
        device_states[device_id].checked = true;
    }
    return device_states[device_id].valid;
}

void uncheck_device(device_id_t device_id) {
    device_states[device_id].checked = false;
}

bool check_and_uncheck_device(device_id_t device_id) {
    bool original_checked = device_states[device_id].checked;
    device_states[device_id].checked = false;
    return original_checked;
}

const char *device_name_of(device_id_t device_id) {
    static const char *names[NUM_DEVICES] = {
        "MAGNETOMETER_ID", "PHOTODIODE_ID", "GYROSCOPE_ID", "MRAM_ID",    "MAGNETORQUERS_ID",
        "SBAND_ID",        "UHF_ID",        "EPS_ID",       "DISPLAY_ID", "CAMERA_ID",
    };
    return names[device_id];
}

static bool mram_io_ready(void) {
    if (mram_io != NULL) {
        return true;
    }

    spi_m_sync_enable(&SPI_MRAM);
    return spi_m_sync_get_io_descriptor(&SPI_MRAM, &mram_io) == 0 && mram_io != NULL;
}

static void mram_select(uint8_t chip) {
    if (chip == 0u) {
        gpio_set_pin_level(MRAM1_CS, false);
    } else if (chip == 1u) {
        gpio_set_pin_level(MRAM2_CS, false);
    } else if (chip == 2u) {
        gpio_set_pin_level(MRAM3_CS, false);
    }
}

static void mram_deselect(uint8_t chip) {
    if (chip == 0u) {
        gpio_set_pin_level(MRAM1_CS, true);
    } else if (chip == 1u) {
        gpio_set_pin_level(MRAM2_CS, true);
    } else if (chip == 2u) {
        gpio_set_pin_level(MRAM3_CS, true);
    }
}

static void mram_deselect_all(void) {
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);
}

static void mram_jesd252_reset_all(void) {
    spi_m_sync_disable(&SPI_MRAM);

    mram_deselect_all();
    gpio_set_pin_level(MRAM_SCK, false);
    gpio_set_pin_level(MRAM_MOSI, false);
    gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);
    gpio_set_pin_direction(MRAM_MOSI, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(MRAM_SCK, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_function(MRAM_MOSI, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(MRAM_MISO, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(MRAM_MISO, GPIO_PULL_OFF);
    gpio_set_pin_function(MRAM_MISO, GPIO_PIN_FUNCTION_OFF);
    delay_us(1);

    // JESD252 samples IO0 on four CS rising edges with CK held low (datasheet Fig. 37).
    // This only recovers the interface: no DFIM, OTP, configuration writes, or erase.
    for (uint8_t chip = 0; chip < MRAM_CHIP_COUNT; chip++) {
        for (uint8_t pulse = 0; pulse < 4u; pulse++) {
            gpio_set_pin_level(MRAM_MOSI, (pulse & 1u) != 0u);
            delay_us(1);
            mram_select(chip);
            delay_us(1);
            mram_deselect(chip);
            delay_us(1);
        }
        delay_ms(1);
    }

    mram_deselect_all();
    SPI_MRAM_PORT_init();
    spi_m_sync_enable(&SPI_MRAM);
}

static bool mram_write(const uint8_t *data, size_t length) {
    int32_t result = io_write(mram_io, data, (uint16_t)length);
    if (result != (int32_t)length) {
        warning("MRAM SPI write returned %ld, expected %u bytes\n", (long)result, (unsigned int)length);
        return false;
    }
    return true;
}

static bool mram_read(uint8_t *data, size_t length) {
    int32_t result = io_read(mram_io, data, (uint16_t)length);
    if (result != (int32_t)length) {
        warning("MRAM SPI read returned %ld, expected %u bytes\n", (long)result, (unsigned int)length);
        return false;
    }
    return true;
}

static bool mram_read_id(uint8_t chip, uint8_t id[3]) {
    const uint8_t cmd = MRAM_CMD_RDID;

    mram_select(chip);
    bool success = mram_write(&cmd, 1u) && mram_read(id, 3u);
    mram_deselect(chip);
    return success;
}

static bool mram_read_status(uint8_t chip, uint8_t *status) {
    const uint8_t cmd = MRAM_CMD_RDSR;

    mram_select(chip);
    bool success = mram_write(&cmd, 1u) && mram_read(status, 1u);
    mram_deselect(chip);
    return success;
}

static bool mram_write_enable(uint8_t chip) {
    const uint8_t cmd = MRAM_CMD_WREN;

    mram_select(chip);
    bool success = mram_write(&cmd, 1u);
    mram_deselect(chip);
    return success;
}

static bool mram_read_bytes(uint8_t chip, uint32_t address, uint8_t *data, size_t length) {
    uint8_t header[4] = {
        MRAM_CMD_READ,
        (uint8_t)(address >> 16u),
        (uint8_t)(address >> 8u),
        (uint8_t)address,
    };

    mram_select(chip);
    bool success = mram_write(header, sizeof(header)) && mram_read(data, length);
    mram_deselect(chip);
    return success;
}

static bool mram_write_bytes(uint8_t chip, uint32_t address, const uint8_t *data, size_t length) {
    uint8_t header[4] = {
        MRAM_CMD_WRITE,
        (uint8_t)(address >> 16u),
        (uint8_t)(address >> 8u),
        (uint8_t)address,
    };

    if (!mram_write_enable(chip)) {
        return false;
    }

    mram_select(chip);
    bool success = mram_write(header, sizeof(header)) && mram_write(data, length);
    mram_deselect(chip);
    return success;
}

static bool mram_id_is_expected(const uint8_t id[3]) {
    return id[0] == MRAM_EXPECTED_ID_0 && id[1] == MRAM_EXPECTED_ID_1 && id[2] == MRAM_EXPECTED_ID_2;
}

static bool mram_buffers_match(const uint8_t *lhs, const uint8_t *rhs, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (lhs[i] != rhs[i]) {
            warning("MRAM mismatch at 0x%06lx: read %02x, expected %02x\n", (unsigned long)(MRAM_TEST_ADDRESS + i), (unsigned int)lhs[i],
                    (unsigned int)rhs[i]);
            return false;
        }
    }

    return true;
}

static bool check_mram_chip(uint8_t chip) {
    uint8_t id[3] = {0};
    uint8_t original[MRAM_TEST_SIZE] = {0};
    uint8_t test_pattern[MRAM_TEST_SIZE] = {
        0xA5, 0x5A, 0x3C, 0xC3, 0x96, 0x69, 0x12, 0x21, 0x4D, 0xD4, 0x87, 0x78, 0xBE, 0xEB, 0x53, 0x35,
    };
    uint8_t readback[MRAM_TEST_SIZE] = {0};

    for (size_t i = 0; i < MRAM_TEST_SIZE; i++) {
        test_pattern[i] ^= chip;
    }

    if (!mram_read_id(chip, id)) {
        warning("MRAM chip %u ID transfer failed\n", chip + 1u);
        return false;
    }
    info("MRAM chip %u ID = %02x %02x %02x (expect 6b bb 14)\n", chip + 1u, (unsigned int)id[0], (unsigned int)id[1], (unsigned int)id[2]);
    if (!mram_id_is_expected(id)) {
        warning("MRAM chip %u ID mismatch; skipping memory writes\n", chip + 1u);
        return false;
    }

    uint8_t status = 0;
    if (!mram_read_status(chip, &status)) {
        warning("MRAM chip %u status transfer failed\n", chip + 1u);
        return false;
    }
    info("MRAM chip %u status = %02x (WIP=%u WEL=%u BP/TB=%02x)\n", chip + 1u, (unsigned int)status, (unsigned int)(status & 1u),
         (unsigned int)((status >> 1u) & 1u), (unsigned int)(status & 0x7Cu));

    if (!mram_read_bytes(chip, MRAM_TEST_ADDRESS, original, MRAM_TEST_SIZE)) {
        warning("MRAM chip %u original-data read failed\n", chip + 1u);
        return false;
    }

    if (!mram_write_bytes(chip, MRAM_TEST_ADDRESS, test_pattern, MRAM_TEST_SIZE)) {
        warning("MRAM chip %u test-pattern write failed; attempting restoration\n", chip + 1u);
        if (!mram_write_bytes(chip, MRAM_TEST_ADDRESS, original, MRAM_TEST_SIZE)) {
            warning("MRAM chip %u restoration write also failed; test region may be modified\n", chip + 1u);
        } else {
            warning("MRAM chip %u restoration attempted but not verified; test region may be modified\n", chip + 1u);
        }
        return false;
    }

    if (!mram_read_bytes(chip, MRAM_TEST_ADDRESS, readback, MRAM_TEST_SIZE) ||
        !mram_buffers_match(readback, test_pattern, MRAM_TEST_SIZE)) {
        warning("MRAM chip %u test-pattern verification failed; attempting restoration\n", chip + 1u);
        if (!mram_write_bytes(chip, MRAM_TEST_ADDRESS, original, MRAM_TEST_SIZE)) {
            warning("MRAM chip %u restoration write failed; test region may be modified\n", chip + 1u);
        } else {
            warning("MRAM chip %u restoration attempted but not verified; test region may be modified\n", chip + 1u);
        }
        return false;
    }

    if (!mram_write_bytes(chip, MRAM_TEST_ADDRESS, original, MRAM_TEST_SIZE)) {
        warning("MRAM chip %u restoration write failed; test region may be modified\n", chip + 1u);
        return false;
    }

    if (!mram_read_bytes(chip, MRAM_TEST_ADDRESS, readback, MRAM_TEST_SIZE) || !mram_buffers_match(readback, original, MRAM_TEST_SIZE)) {
        warning("MRAM chip %u restoration verification failed; test region may be modified\n", chip + 1u);
        return false;
    }

    info("MRAM chip %u read/write and restoration verified\n", chip + 1u);
    return true;
}

bool check_mram(void) {
    if (!mram_io_ready()) {
        warning("MRAM SPI interface was not ready\n");
        return false;
    }

    mram_deselect_all();
    delay_ms(50);

    for (uint8_t chip = 0; chip < MRAM_CHIP_COUNT; chip++) {
        uint8_t id[3] = {0};
        if (mram_read_id(chip, id)) {
            info("MRAM chip %u before JESD252 ID = %02x %02x %02x\n", chip + 1u, (unsigned int)id[0], (unsigned int)id[1],
                 (unsigned int)id[2]);
        } else {
            warning("MRAM chip %u before JESD252 ID transfer failed\n", chip + 1u);
        }
    }

    info("MRAM JESD252 reset-only recovery on all three chips; no factory initialization\n");
    mram_jesd252_reset_all();

    bool identified[MRAM_CHIP_COUNT] = {false};
    for (uint8_t chip = 0; chip < MRAM_CHIP_COUNT; chip++) {
        uint8_t id[3] = {0};
        if (mram_read_id(chip, id)) {
            info("MRAM chip %u after JESD252 ID = %02x %02x %02x\n", chip + 1u, (unsigned int)id[0], (unsigned int)id[1],
                 (unsigned int)id[2]);
            identified[chip] = mram_id_is_expected(id);
        } else {
            warning("MRAM chip %u after JESD252 ID transfer failed\n", chip + 1u);
        }
        if (!identified[chip]) {
            warning("MRAM chip %u not identified after JESD252; skipping memory writes\n", chip + 1u);
        }
    }

    bool all_okay = true;
    for (uint8_t chip = 0; chip < MRAM_CHIP_COUNT; chip++) {
        if (!identified[chip] || !check_mram_chip(chip)) {
            warning("MRAM chip %u failed connectivity/read-write verification\n", chip + 1u);
            all_okay = false;
        }
    }

    return all_okay;
}

// ------------------- check function definitions ----------------------
// - these should wrap device drivers/hardware interaction functions

//bool check_magnetometer(void) {
//    return init_rm3100() == SUCCESS;
//}

bool check_display(void) {
    return init_display_hardware() == SUCCESS;
}
