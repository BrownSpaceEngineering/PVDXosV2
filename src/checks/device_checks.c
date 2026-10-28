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

#include "arducam_driver.h"
#include "display_driver.h"
#include "drivers/at86rf215/at86rf215.h"
#include "drivers/at86rf215/regs.h"
#include "drivers/fuel_gauge/fuel_gauge_driver.h"
#include "drivers/gyro/gyro_driver.h"
#include "drivers/mram/mram_driver.h"
#include "drivers/pmb/ina226.h"
#include "drivers/pmb/mcp23017.h"
#include "drivers/pmb/pmb_i2c.h"
#include "drivers/stxg2/stxg2.h"
#include "drivers/thermistor/thermistor_driver.h"
#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"
#include "photodiode_driver.h"

// forward declarations of check functions
bool check_magnetometer(void);
bool check_photodiodes(void);
bool check_gyro(void);
bool check_mram(void);
bool check_camera(void);
bool check_sband(void);
bool check_uhf(void);
bool check_perovskite(void);
bool check_fuel_gauges(void);
bool check_display(void);
bool check_thermistors(void);
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

// ~~~ for safety, these structures should be local to this source file ~~~
typedef struct device_check_state {
    bool checked;
    bool valid;
} device_check_state_t;

static device_check_state_t device_states[NUM_DEVICES] = {0}; // inits to {false, false} for each entry

static bool (*device_check_functions[NUM_DEVICES])(void) = {
    //    func ptr      |  device_id_t
    // -------------------------------
    &check_magnetometer, // MAGNETOMETER_ID
    &check_photodiodes,  // PHOTODIODE_ID
    &check_gyro,         // GYROSCOPE_ID
    &check_mram,         // MRAM_ID
    NULL,                // MAGNETORQUERS_ID
    &check_sband,        // SBAND_ID
    &check_uhf,          // UHF_ID
    &check_fuel_gauges,  // EPS_ID — the four battery fuel gauges
    &check_display,      // DISPLAY_ID — init done inside the display task post-scheduler
    &check_camera,       // CAMERA_ID
    &check_perovskite,   // PEROVSKITE_ID
    &check_thermistors,  // THERMISTOR_ID
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
        "MAGNETOMETER_ID", "PHOTODIODE_ID", "GYROSCOPE_ID", "MRAM_ID",   "MAGNETORQUERS_ID", "SBAND_ID",
        "UHF_ID",          "EPS_ID",        "DISPLAY_ID",   "CAMERA_ID", "PEROVSKITE_ID",    "THERMISTOR_ID",
    };
    return names[device_id];
}

// ------------------- check function definitions ----------------------
// - these should wrap device drivers/hardware interaction functions

bool check_magnetometer(void) {
    return init_rm3100() == SUCCESS;
}

bool check_gyro(void) {
    return init_gyro_hardware() == SUCCESS;
}

// Passes only if all three MRAM chips report MRAM_STATE_OK (triplicated storage, majority-voted reads).
bool check_mram(void) {
    mram_status_t status = mram_init();
    bool all_okay = true;
    for (uint8_t mram = 1; mram <= 3; mram++) {
        uint8_t state = get_mram_state(status, mram);
        info("MRAM%u state: %u (1 = OK)\n", mram, state);
        all_okay = all_okay && (state == MRAM_STATE_OK);
    }
    return all_okay;
}

bool check_camera(void) {
    if (init_arducam_hardware() != SUCCESS) {
        return false;
    }
#if defined(BURN_SEQUENCE) && defined(BURN_SEQUENCE_CAMERA_CHECK_CAPTURE)
    // main.c configures RTT channel 2 before the device checks in the burn sequence
    return capture_rtt() == SUCCESS;
#else
    return true;
#endif
}

// S-band (STXG2): stxg2_init() powers the radio, waits for boot, checks the "STX2" ID and does a
// scratchpad write/readback (from s-band-driver)
bool check_sband(void) {
    return stxg2_init() == SUCCESS;
}

// UHF (AT86RF215): reset + init, part-number check, then write/readback/restore of both RFn_CS
// registers to exercise the write path (from at86-tx-rx-test's test_at86)
static struct at86rf215 uhf_check_dev;

static bool check_uhf_cs_write(const char *name, uint16_t reg) {
    uint8_t original = 0, readback = 0;
    if (at86rf215_reg_read_8(&uhf_check_dev, &original, reg) != AT86RF215_OK) {
        warning("uhf check: %s_CS read failed\n", name);
        return false;
    }
    const uint8_t expected = (uint8_t)~original;
    bool okay = at86rf215_reg_write_8(&uhf_check_dev, expected, reg) == AT86RF215_OK &&
        at86rf215_reg_read_8(&uhf_check_dev, &readback, reg) == AT86RF215_OK && readback == expected;
    if (!okay) {
        warning("uhf check: %s_CS wrote 0x%02x, read back 0x%02x\n", name, expected, readback);
    }
    // Always restore the original channel spacing
    okay = (at86rf215_reg_write_8(&uhf_check_dev, original, reg) == AT86RF215_OK) && okay;
    return okay;
}

bool check_uhf(void) {
    spi_m_sync_enable(&SPI_UHF);

    uhf_check_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    uhf_check_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    uhf_check_dev.rf_femode_09 = AT86RF215_RF_FEMODE0;
    uhf_check_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    uhf_check_dev.xo_fs = 0;
    uhf_check_dev.xo_trim = 0;
    uhf_check_dev.irqmm = 0;
    uhf_check_dev.irqp = 0;
    uhf_check_dev.pad_drv = AT86RF215_RF_DRV2;

    int ret = at86rf215_init(&uhf_check_dev);
    if (ret != AT86RF215_OK) {
        warning("uhf check: at86rf215_init failed (%d)\n", ret);
        return false;
    }
    ret = at86rf215_conn_check(&uhf_check_dev);
    info("uhf check: family 0x%02x version 0x%02x (conn_check %d)\n", uhf_check_dev.priv.family, uhf_check_dev.priv.version, ret);
    if (ret != AT86RF215_OK) {
        return false;
    }
    return check_uhf_cs_write("RF09", REG_RF09_CS) && check_uhf_cs_write("RF24", REG_RF24_CS);
}

// Perovskite measurement board: I2C bus, INA226 + MCP23017 present, INA226 manufacturer/die ID.
// Read-only identity checks from pmb-driver-mock's test_pmb sanity checks (no DAC/ADG734 actuation).
bool check_perovskite(void) {
    if (pmb_i2c_init() != SUCCESS) {
        warning("perovskite check: PMB I2C bus enable failed\n");
        return false;
    }
    if (pmb_i2c_probe(INA226_I2C_ADDR) != SUCCESS) {
        warning("perovskite check: INA226 did not ACK at 0x%02x\n", INA226_I2C_ADDR);
        return false;
    }
    if (pmb_i2c_probe(MCP23017_I2C_ADDR) != SUCCESS) {
        warning("perovskite check: MCP23017 did not ACK at 0x%02x\n", MCP23017_I2C_ADDR);
        return false;
    }
    uint16_t mfg = 0, die = 0;
    if (ina226_read_reg(INA226_MANUFACTURER_ID, &mfg) != SUCCESS || mfg != INA226_MFG_ID ||
        ina226_read_reg(INA226_DIE_ID, &die) != SUCCESS || (die & 0xFFF0U) != INA226_DIE_ID_VALUE) {
        warning("perovskite check: INA226 ID mfg=0x%04x die=0x%04x\n", mfg, die);
        return false;
    }
    return true;
}

// Fuel gauges (EPS): runs the driver init, which releases the mux reset, then selects each of the four
// mux channels and reads that gauge's DevName and config. Passes only if the mux and all four gauges respond.
// Read-only (no NV writes), so the fuel gauge task can run it again later.
bool check_fuel_gauges(void) {
    return init_fuel_gauges() == SUCCESS;
}

bool check_display(void) {
    return init_display_hardware() == SUCCESS;
}

// Photodiodes: reads all 18 channels and logs each code. Fails if any ADC read fails, or if every channel reads the
// same value above the light floor (e.g. all pinned at full scale), which a working array never does. A fully dark
// array only warns, since startup can happen in eclipse, where every channel legitimately reads near zero.
#define PHD_CHECK_LIGHT_FLOOR 50 // ADC code a lit channel should exceed

bool check_photodiodes(void) {
    if (init_photodiodes() != SUCCESS) {
        warning("photodiode check: init failed\n");
        return false;
    }

    uint16_t codes[PHD_COUNT];
    if (phd_read_all(codes) != SUCCESS) {
        warning("photodiode check: ADC read failed\n");
        return false;
    }

    uint16_t lo = 0xFFFF, hi = 0;
    uint8_t lit = 0;
    for (uint8_t i = 0; i < PHD_COUNT; i++) {
        info("photodiode check: PHD_%u code %u\n", i, codes[i]);
        if (codes[i] > PHD_CHECK_LIGHT_FLOOR) {
            lit++;
        }
        if (codes[i] < lo) {
            lo = codes[i];
        }
        if (codes[i] > hi) {
            hi = codes[i];
        }
    }
    info("photodiode check: %u/%u channels above %u, range %u..%u\n", lit, PHD_COUNT, PHD_CHECK_LIGHT_FLOOR, lo, hi);

    if (lit == 0) {
        warning("photodiode check: no channel above the light floor (dark/eclipse, or array disconnected)\n");
        return true;
    }
    if (hi == lo) {
        warning("photodiode check: all channels read %u (array not responding?)\n", lo);
        return false;
    }
    return true;
}

// Thermistors + heaters: runs the driver init (ADC1 setup, both heater enables driven off), then reads each
// thermistor and requires a non-railed ADC code (a railed code means an open or shorted divider) and a plausible
// temperature. The heaters are left off: they have no feedback the MCU can read, and switching them on is
// actuation, so the check only confirms their enables were driven off.
#define THERM_CHECK_MIN_C -40.0f
#define THERM_CHECK_MAX_C 85.0f

bool check_thermistors(void) {
    if (init_thermistors() != SUCCESS) {
        warning("thermistor check: init failed\n");
        return false;
    }

    bool all_okay = true;
    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        uint16_t code = 0;
        if (therm_read_raw(i, &code) != SUCCESS) {
            warning("thermistor check: T%u ADC read failed\n", i);
            all_okay = false;
            continue;
        }
        if (code == 0 || (float)code / THERM_ADC_FS >= THERM_K_OPEN) {
            warning("thermistor check: T%u code %u railed (open or shorted divider)\n", i, code);
            all_okay = false;
            continue;
        }
        float temp_c = 0.0f;
        if (therm_read_celsius(i, &temp_c) != SUCCESS || temp_c < THERM_CHECK_MIN_C || temp_c > THERM_CHECK_MAX_C) {
            warning("thermistor check: T%u code %u -> %ld mC out of range\n", i, code, (long)(temp_c * 1000.0f));
            all_okay = false;
            continue;
        }
        info("thermistor check: T%u code %u -> %ld mC\n", i, code, (long)(temp_c * 1000.0f));
    }

    for (uint8_t h = 0; h < THERM_HEATER_COUNT; h++) {
        if (heater_is_on(h)) {
            warning("thermistor check: heater %u not off after init\n", h + 1);
            all_okay = false;
        }
    }
    return all_okay;
}
