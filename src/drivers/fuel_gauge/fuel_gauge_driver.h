#ifndef FUEL_GAUGE_DRIVER_H
#define FUEL_GAUGE_DRIVER_H

/**
 * fuel_gauge_driver.h
 *
 * Driver for the battery fuel-gauge subsystem: four Maxim MAX17205G+ ModelGauge-m5
 * fuel gauges (one per 2S battery pack), each on its own downstream channel of a
 * TI TCA9546A 4-channel I2C mux. Because the mux isolates the gauges, all four share
 * the same I2C address; firmware selects one channel at a time. A single active-low
 * ALRT line is shared by all four gauges, so an alert requires scanning all four.
 *
 * Bus: the mux + gauges live on the I2C_CAMERA descriptor (SERCOM6 on this branch),
 * shared with the Arducam (0x30), an INA226 (0x40) and an MCP23017 (0x20). The MAX
 * addresses (0x36/0x0B) are only visible while a mux channel is open; this driver
 * closes all channels when done so nothing downstream shadows the bus. See the plan
 * (I2C address map) for the full collision analysis.
 */

#include "stdint.h"
#include "globals.h"
#include "atmel_start.h"
#include "logging.h"
#include "string.h"

// ---- Bus / device addresses (7-bit) ----
#define FG_I2C          I2C_CAMERA // shared bus; SERCOM6 on this branch
#define FG_MUX_ADDR     0x70       // TCA9546A (A2:A0 = 000)
#define FG_MAX_ADDR_LOW 0x36       // MAX17205 m5 page: regs 0x000-0x0FF and 0x180-0x1FF
#define FG_MAX_ADDR_HIGH 0x0B      // MAX17205 SBS page: regs 0x100-0x17F

// ---- TCA9546A channel-select control byte (bits [3:0] enable channels 0-3) ----
#define FG_MUX_CH_NONE 0x00
#define FG_MUX_CH(n)   (1u << (n)) // channel 0..3

#define FG_NUM_PACKS 4

// ---- MAX17205 register addresses (12-bit memory map; page picked by driver) ----
#define MAX17205_REG_STATUS     0x000 // alert/fault flags
#define MAX17205_REG_REPSOC     0x006 // reported state of charge (1/256 %)
#define MAX17205_REG_TEMP       0x008 // temperature (1/256 degC, signed)
#define MAX17205_REG_VCELL      0x009 // per-cell voltage (78.125 uV/LSB)
#define MAX17205_REG_CURRENT    0x00A // current (1.5625 uV / Rsense per LSB, signed)
#define MAX17205_REG_DESIGNCAP  0x018 // design capacity
#define MAX17205_REG_DEVNAME    0x021 // device type / firmware identity
#define MAX17205_REG_PACKCFG    0x0BD // active pack config (NCELLS field)
#define MAX17205_REG_CELL2      0x0D7 // cell 2 voltage (78.125 uV/LSB)
#define MAX17205_REG_CELL1      0x0D8 // cell 1 voltage (78.125 uV/LSB)
#define MAX17205_REG_BATT       0x0DA // total pack voltage (1.25 mV/LSB)
#define MAX17205_REG_NDESIGNCAP 0x1B3 // NV design capacity shadow
#define MAX17205_REG_NPACKCFG   0x1B5 // NV pack config (NCELLS field)

// Cell-voltage LSB (VCell/Cell1/Cell2): 78.125 uV per bit.
#define MAX17205_CELL_LSB_V 78.125e-6f

// Rsense (ohms) used for the Current -> amps conversion. Unknown at bring-up; adjust
// once the board value is confirmed (see plan open items).
#ifndef FG_RSENSE_OHMS
    #define FG_RSENSE_OHMS 0.010f
#endif

// Decoded per-pack telemetry.
typedef struct {
    float soc_pct;      // state of charge, %
    float pack_voltage; // total pack voltage, V
    float current_a;    // current, A (depends on FG_RSENSE_OHMS)
    float temp_c;       // temperature, degC
} fg_reading_t;

status_t init_fuel_gauges(void);
status_t fg_read_pack(uint8_t channel, fg_reading_t *out);
status_t fg_read_all(fg_reading_t out[FG_NUM_PACKS]);
bool fg_alrt_asserted(void);
void fg_scan_alerts(void);

// Selects `channel`, reads one 16-bit register from that gauge, then closes the mux.
// Useful for probing arbitrary registers (DevName, per-cell voltages) and bring-up debug.
status_t fg_read_raw(uint8_t channel, uint16_t reg, uint16_t *out);

#endif // FUEL_GAUGE_DRIVER_H
