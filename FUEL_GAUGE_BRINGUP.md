# Fuel Gauge Bring-up Debug Log (MAX17205)

Working notes from bench bring-up of a MAX17205 fuel gauge on the `simon-drivers-baremetal`
branch. Goal: get the gauge to respond on I²C so `fg_read_reg16` works.

## Physical setup

- **Flight PCB attached to a dev board.** The TCA9546A mux is **not populated yet**.
- By design all four MAX17205 gauges sit **behind** the mux (gauge SDA/SCL = the mux
  *downstream* channel nets; mux upstream = main I²C bus).
- For bench test: **one gauge** jumpered directly to the dev board's **SERCOM6** I²C:
  - **PD09 = SDA** (SERCOM6 PAD0)
  - **PD08 = SCL** (SERCOM6 PAD1)
- An Arducam (OV2640) is also on this bus, wired upstream, and **ACKs at 0x30**.

## Datasheet facts (MAX17201-MAX17215.pdf)

- **I²C address**: Table 27 (p100) lists `6Ch`/`6Dh` (8-bit) → **7-bit 0x36** for regs
  000h–0FFh and 180h–1FFh; `16h`/`17h` → **0x0B** for 100h–17Fh. No address-select pins
  (fixed address — that's why the design needs the mux for 4 identical gauges).
- **Supply**: MAX17205 `VBATT` operating range **4.2–20 V**, needs **≥4.2 V to start**
  (3.3 V is NOT enough). Powered from the cell stack, not 3V3 — bench-emulate with a PSU.
- **Internal regulators / bypass caps**: `REG2` ≈ 1.8 V (digital core) and `REG3` ≈ 3.4 V
  each need an external **0.47 µF** cap. The SDA/SCL output levels are referenced to
  **VREG3**, so if REG3 isn't up, the I²C front-end can't drive.
- 16-bit registers are **little-endian** (LSB first).
- Read protocol (Fig 74): write mem-address → repeated-START → read. A bare read is not a
  valid transaction for this part.

## Firmware state (all verified correct against the datasheet)

- `fg_read_reg16` (the path in use via the mux-select-commented `init_fuel_gauges`):
  `set_slaveaddr(0x36)` + `i2c_m_sync_cmd_read`. Mux-free. Address shift confirmed
  (`ADDR = addr<<1 | R/W`, so 0x36 → 0x6C on wire; cross-checked by the camera at 0x30).
- Two-address page split and LSB-first decode match Table 27 / the datasheet.
- **Bug found & fixed**: `fg_i2c_scan` originally used a bare *read* probe (invalid for the
  MAX17205); changed to a **write** probe (address + mem-addr byte + STOP).
- **Added diagnostics**:
  - `fg_i2c_scan()` — probes 0x00–0x7F, logs which ACK. Called at top of `main` bring-up.
  - `fg_read_reg16` now logs the **raw I²C return code** via `fg_i2c_err_str()`
    (distinguishes `NACK -2` = no device vs `BUS -5` = electrical fault).
  - `pin_toggle_test()` in `main.c` — drives PD09/PD08 as plain GPIO in a 4-state loop
    (SDA/SCL high/low combos, 3 s each) so each line can be traced with a multimeter to the
    gauge pins. **Currently called in `main()` and loops forever — remove the call to
    resume normal boot.**

## Ruled out

- Firmware / driver logic — `fg_read_reg16` is correct and mux-free; the scan validated by
  the camera ACK.
- Bus electrical (pull-ups, levels, timing) — camera ACKs at 0x30 on the same bus.
- Addressing — 0x36 provably correct on the wire.
- VBATT present at the pin, common ground — confirmed by user.
- SDA/SCL swap at the gauge — tried both orientations, still NACK.

## Current symptom

`fg_read_reg16` / scan return **NACK (-2)** for 0x36: bus healthy, address sent correctly,
**nothing acknowledges at 0x36.** It's a connectivity/device issue, not firmware.

## Leading hypothesis & next steps

1. **The jumpers may not actually land on the gauge's net.** On the flight PCB the gauge's
   SDA/SCL is the *downstream-of-mux* net, separate from the main bus. If PD09/PD08 are
   tapped on a bus header that's really the mux *upstream* side, the gauge is still stranded
   at the empty mux footprint. **Continuity-check: PD09 → gauge SDA pin, PD08 → gauge SCL
   pin.** (Flash the `pin_toggle_test` build and trace each line with a meter.)
2. If genuinely connected: check (by schematic/BOM, not probing the flight PCB) that the
   **REG2/REG3 0.47 µF caps are populated** and the part is the **I²C variant (MAX172_0_5)**,
   not the 1-Wire 172_1_5.
3. Alternative to jumpering: **populate the TCA9546A** (or solder-bridge its footprint,
   upstream SDA/SCL → the gauge's channel SDx/SCx), then re-enable `fg_mux_select` in the
   driver and select the channel.

## Relevant files

- `src/drivers/fuel_gauge/fuel_gauge_driver.c` / `.h` — driver, scan, error-code logging.
- `src/main.c` — `pin_toggle_test()` + bring-up wiring (scan call, mux-select commented out).
- Datasheets at repo root: `MAX17201-MAX17215.pdf`, `tca9546a.pdf`.
