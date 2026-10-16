/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully, creates the high-level OS integrity tasks along with the Cosmic Monkey task, and finally starts
 * the FreeRTOS scheduler.
 *
 * Created: November 20, 2023
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu, Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"
#include "reflash_task.h"

#define RSTC_RCAUSE (0x40000C00UL)  // Reset Cause Register

#include "SEGGER_RTT.h"
#include "checks/device_checks.h"
#include "cosmic_monkey_task.h"
#include "driver_init.h"
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_list.h"
#include "tests/test.h"

cosmic_monkey_task_arguments_t cm_args = {0};

void init_logging(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
}

void report_reset_cause(void) {
    uint8_t cause = *((uint8_t *)RSTC_RCAUSE);

    info_impl("[+] Last reset cause(s):\n");
    if (cause & 0x01) {
        info_impl("- Power on Reset\n");
    }
    if (cause & 0x02) {
        info_impl("- Brown-Out Detector Core Reset\n");
    }
    if (cause & 0x04) {
        info_impl("- Brown-Out Detector VDD Reset\n");
    }
    if (cause & 0x10) {
        info_impl("- NVM Reset\n");
    }
    if (cause & 0x20) {
        info_impl("- External Reset\n");
    }
    if (cause & 0x40) {
        info_impl("- Watchdog Reset\n");
    }
    if (cause & 0x80) {
        info_impl("- System Reset Request\n");
    }
}

// ======================================================================================
// MRAM factory initialization (Everspin EST 3000 Rev 2.1, section 13, <=64Mbit path)
// ======================================================================================
// The EMxxLX device state is undefined after PCB solder reflow; Everspin requires a
// one-time Device Factory Initialization (DFIM) before the part will operate. This runs
// the EST 3000 command sequence over SPI_MRAM in single-SPI (1S-1S-1S) mode.
//
// *** DESTRUCTIVE: erases the entire array and the OTP area. Intended to run ONCE per
//     chip after assembly. Remove the MRAM_RUN_FACTORY_INIT define after a successful run.
//
// Step 1 is the JESD252 signal reset (see mram_jesd_reset below), which forces the device
// interface back to single-SPI 1S regardless of the nonvolatile I/O-mode register. This is
// required by EST 3000 step 1 and is the only reset that works on a reflow-fresh part whose
// NV config is undefined (a software 0x66/0x99 reset would just reload that garbage config).
//
// Read-verify steps in EST 3000 are explicitly optional (and a full-array verify at the
// 50 kHz bus clock would take minutes), so they are omitted; WIP is polled after every
// write instead.

#define MRAM_CMD_RESET_EN   0x66
#define MRAM_CMD_RESET_MEM  0x99
#define MRAM_CMD_WREN       0x06
#define MRAM_CMD_RDSR       0x05
#define MRAM_CMD_WRSR       0x01
#define MRAM_CMD_WRNVCR     0xB1
#define MRAM_CMD_WRVCR      0x81
#define MRAM_CMD_OTP_WRITE  0x42
#define MRAM_CMD_BULK_ERASE 0xC7

#define MRAM_VCR_DFIM_ADDR 0x1E // DFIM register (volatile)
#define MRAM_DFIM_KEY      0x6B // Everspin manufacturer ID, used as the DFIM-enter key
#define MRAM_VCR8_ADDR     0x08 // volatile config reg 8: OTP lock + erase data-state
#define MRAM_OTP_BYTES     257  // 256-byte OTP array + 1 control byte
#define MRAM_SR_WIP        0x01 // status register bit 0 = Write In Progress (1 = busy)

// One command: assert CS, shift out the whole opcode+addr+data buffer, deassert CS.
static void mram_cmd(uint32_t cs, const uint8_t *buf, uint32_t n) {
    struct spi_xfer x = {.txbuf = (uint8_t *)buf, .rxbuf = NULL, .size = n};
    gpio_set_pin_level(cs, false);
    spi_m_sync_transfer(&SPI_MRAM, &x);
    gpio_set_pin_level(cs, true);
}

// Poll the status register until WIP clears (write/erase complete). Bulk erase is the
// slow case (~16 ms for 8 Mb), so the 1 ms poll with a generous ceiling is plenty.
static void mram_wait_wip(uint32_t cs) {
    for (int i = 0; i < 1000; i++) {
        uint8_t cmd = MRAM_CMD_RDSR;
        uint8_t sr = 0xFF;
        struct spi_xfer w = {.txbuf = &cmd, .rxbuf = NULL, .size = 1};
        struct spi_xfer r = {.txbuf = NULL, .rxbuf = &sr, .size = 1};
        gpio_set_pin_level(cs, false);
        spi_m_sync_transfer(&SPI_MRAM, &w);
        spi_m_sync_transfer(&SPI_MRAM, &r);
        gpio_set_pin_level(cs, true);
        if (!(sr & MRAM_SR_WIP)) {
            return;
        }
        delay_ms(1);
    }
    warning("[MRAM INIT] WIP poll timed out\n");
}

static void mram_wren(uint32_t cs) {
    uint8_t c = MRAM_CMD_WREN;
    mram_cmd(cs, &c, 1);
}

// Write Status Register (0x01): opcode + 1 data byte, no address.
static void mram_wrsr(uint32_t cs, uint8_t val) {
    mram_wren(cs);
    uint8_t b[2] = {MRAM_CMD_WRSR, val};
    mram_cmd(cs, b, sizeof(b));
    mram_wait_wip(cs);
}

// Write a config register (0x81 volatile / 0xB1 nonvolatile): opcode + 3-byte address + data.
static void mram_wrcfg(uint32_t cs, uint8_t opcode, uint8_t addr, uint8_t val) {
    mram_wren(cs);
    uint8_t b[5] = {opcode, 0x00, 0x00, addr, val};
    mram_cmd(cs, b, sizeof(b));
    mram_wait_wip(cs);
}

// OTP Write (0x42): fill all 257 OTP bytes with val via address auto-increment.
static void mram_otp_fill(uint32_t cs, uint8_t val) {
    mram_wren(cs);
    uint8_t b[4 + MRAM_OTP_BYTES];
    b[0] = MRAM_CMD_OTP_WRITE;
    b[1] = 0x00;
    b[2] = 0x00;
    b[3] = 0x00;
    for (uint32_t i = 0; i < MRAM_OTP_BYTES; i++) {
        b[4 + i] = val;
    }
    mram_cmd(cs, b, sizeof(b));
    mram_wait_wip(cs);
}

static void mram_bulk_erase(uint32_t cs) {
    mram_wren(cs);
    uint8_t c = MRAM_CMD_BULK_ERASE;
    mram_cmd(cs, &c, 1);
    mram_wait_wip(cs);
}

// JESD252 "Reset with Signal Sequence" (datasheet section 18.3, Figure 37; EST 3000 step 1
// and section 3). This is the ONLY reset that forces the interface back to single-SPI 1S
// regardless of the nonvolatile I/O-mode register, so it must run first on a reflow-fresh
// part whose NV config is undefined. A software reset (0x66/0x99) can't do this: it reloads
// the (possibly garbage) NV I/O mode.
//
// Sequence (bit-banged with SERCOM0 SCK/MOSI temporarily remapped to GPIO):
//   - CK held static low (SPI mode 0); IO[1]/MISO left High-Z (input).
//   - Four CS# low pulses, each >= tSL/tSH (500 ns), with IO[0]/MOSI driven to the pattern
//     0,1,0,1 (stable across each falling edge: set up in the preceding CS#-high gap, held
//     through the pulse). The device resets ~200 ns after the 4th CS# rising edge.
// Register contents are NOT changed by this reset; it only returns control logic to a known
// 1S state so the DFIM/config sequence below can talk to the part.
static void mram_jesd_reset(uint32_t cs) {
    const uint8_t pattern[4] = {0, 1, 0, 1}; // IO[0] value during CS# low pulses 1..4

    // Remap SCK (PB25) and MOSI (PB24) from SERCOM0 to GPIO outputs. MISO (PC18) stays an
    // input = High-Z, as Figure 37 requires for IO[1].
    gpio_set_pin_function(MRAM_SCK, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(MRAM_SCK, false); // CK static low (mode 0)
    gpio_set_pin_function(MRAM_MOSI, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(MRAM_MOSI, GPIO_DIRECTION_OUT);

    gpio_set_pin_level(cs, true); // start deselected
    delay_us(1);                  // >= tSH before first pulse

    for (int i = 0; i < 4; i++) {
        gpio_set_pin_level(MRAM_MOSI, pattern[i] != 0); // set IO[0] before CS# falls (tDVSR)
        delay_us(1);
        gpio_set_pin_level(cs, false); // CS# low pulse (>= tSL = 500 ns)
        delay_us(1);
        gpio_set_pin_level(cs, true); // CS# high (>= tSH = 500 ns); IO[0] held stable (tSDVR)
        delay_us(1);
    }
    delay_us(1); // let the internal reset (~200 ns after 4th rising edge) complete

    // Restore SCK/MOSI to their SERCOM0 pinmux so normal SPI works again.
    gpio_set_pin_function(MRAM_SCK, PINMUX_PB25C_SERCOM0_PAD1);
    gpio_set_pin_function(MRAM_MOSI, PINMUX_PB24C_SERCOM0_PAD0);
}

// Software reset: Reset Enable (0x66) then Reset Memory (0x99). Simpler than the JESD252
// signal reset, but it reloads the nonvolatile I/O-mode register, so it only works if that
// register is already a valid single-SPI mode (e.g. a part that powers up in factory-default
// 1S). It cannot rescue a part whose NV I/O mode is corrupted into a non-1S protocol -- use
// the JESD252 signal reset for that case.
static void mram_software_reset(uint32_t cs) {
    uint8_t c;
    c = MRAM_CMD_RESET_EN;
    mram_cmd(cs, &c, 1);
    c = MRAM_CMD_RESET_MEM;
    mram_cmd(cs, &c, 1);
}

// Runs the full EST 3000 section-13 initialization on a single chip (by CS pin).
// use_jesd_reset selects step 1: true = JESD252 signal reset (required for a reflow-fresh
// part with undefined NV config), false = software 0x66/0x99 reset.
static void mram_factory_init(uint32_t cs, bool use_jesd_reset) {
    // 1) Reset to a known single-SPI 1S interface state.
    if (use_jesd_reset) {
        mram_jesd_reset(cs);
    } else {
        mram_software_reset(cs);
    }
    delay_ms(1);

    // 4) Enter DFIM: write the manufacturer-ID key into volatile register 0x1E.
    mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR_DFIM_ADDR, MRAM_DFIM_KEY);

    // 5+6) Status register then nonvolatile config regs 0x00..0x0C, performed twice.
    //      SR ends at 0x00 (block protection cleared); NVCRs end at 0xFF (default config).
    for (int pass = 0; pass < 2; pass++) {
        mram_wrsr(cs, 0xFF);
        mram_wrsr(cs, 0x00);
        for (uint8_t a = 0x00; a <= 0x0C; a++) {
            mram_wrcfg(cs, MRAM_CMD_WRNVCR, a, 0x00);
        }
        for (uint8_t a = 0x00; a <= 0x0C; a++) {
            mram_wrcfg(cs, MRAM_CMD_WRNVCR, a, 0xFF);
        }
    }

    // 7+8) OTP area to 0xFF then 0x00, performed twice (lock disabled via VCR8=0xF9 each).
    for (int pass = 0; pass < 2; pass++) {
        mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR8_ADDR, 0xF9);
        mram_otp_fill(cs, 0xFF);
        mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR8_ADDR, 0xF9);
        mram_otp_fill(cs, 0x00);
    }

    // 9-12) Bulk-erase the array to 0 then to 1, performed twice. VCR8 sets the erase
    //        data state: 0xFF -> erase to 0x00, 0x7F -> erase to 0xFF (EST 3000 step 9/10).
    for (int pass = 0; pass < 2; pass++) {
        mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR8_ADDR, 0xFF);
        mram_bulk_erase(cs);
        mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR8_ADDR, 0x7F);
        mram_bulk_erase(cs);
    }

    // 13) Exit DFIM.
    mram_wrcfg(cs, MRAM_CMD_WRVCR, MRAM_VCR_DFIM_ADDR, 0x00);
}

// Runs factory init on all three MRAMs. Enables SPI + parks CS high first, same as the
// ID smoke test below. Marked unused: the call in main() is normally commented out because
// it is destructive (full erase); uncomment it to run the one-time init.
__attribute__((unused)) static void mram_init_all(bool use_jesd_reset) {
    const uint32_t cs_pins[3] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};

    spi_m_sync_enable(&SPI_MRAM);
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);
    delay_ms(50);

    for (int i = 0; i < 1; i++) {
        info("[MRAM INIT] Running EST 3000 factory init on MRAM%d (%s reset)...\n", i + 1,
             use_jesd_reset ? "JESD252" : "software");
        mram_factory_init(cs_pins[i], use_jesd_reset);
        info("[MRAM INIT] MRAM%d factory init complete\n", i + 1);
    }
}

// Standalone MRAM smoke test: read the Read-ID (0x9F) of each of the 3 MRAMs over
// SPI_MRAM and log it. A good EM008LX returns 6B BB 14. Runs at OS startup; does
// not use the mram_driver (which fatal-hangs on a bad ID).
static void test_mram(void) {
    const uint32_t cs_pins[3] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};

    spi_m_sync_enable(&SPI_MRAM);
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);
    delay_ms(50);

    for (int i = 0; i < 3; i++) {
        // Mirror the driver's check_device_id exactly: write 0x9F, then read 3 bytes,
        // as two transfers with CS held low across both (same NULL-buffer pattern as
        // the driver's spi_write()/spi_read()).
        uint8_t cmd = 0x05;
        uint8_t id[3] = {0};
        struct spi_xfer wr = {.txbuf = &cmd, .rxbuf = NULL, .size = 1};
        struct spi_xfer rd = {.txbuf = NULL, .rxbuf = id, .size = 1};

        gpio_set_pin_level(cs_pins[i], false);
        int32_t rc_w = spi_m_sync_transfer(&SPI_MRAM, &wr);
        int32_t rc_r = spi_m_sync_transfer(&SPI_MRAM, &rd);
        gpio_set_pin_level(cs_pins[i], true);

        info("[MRAM TEST] MRAM%d ID = %02x %02x %02x (expect 6B BB 14) [rc %ld/%ld]\n", i + 1, id[0], id[1], id[2],
             (long)rc_w, (long)rc_r);
    }
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    init_logging();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info_impl("--- Atmel & Hardware Initialization Complete ---\n");
    info_impl("[+] Build Type: %s\n", BUILD_TYPE);
    info_impl("[+] Build Date: %s\n", BUILD_DATE);
    info_impl("[+] Build Time: %s\n", BUILD_TIME);
    info_impl("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info_impl("[+] Built from commit: %s\n", GIT_COMMIT_HASH);
    report_reset_cause();

    // One-time Everspin EST 3000 factory init (DESTRUCTIVE: full erase).
    // mram_init_all(false);

    // Standalone MRAM ID test (runs before anything that touches MRAM).
    test_mram();

    reflash_bootloaders(false);

    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", check_all_devices_on_startup());

/* -------------------------------------- TESTS ---------------------------------------------- */
#ifdef UNITTEST
    tests_run();
#endif

    // Initialize a mutex wrapping the shared PVDX task list struct
    task_list_mutex = xSemaphoreCreateMutexStatic(&task_list_mutex_buffer);

    if (task_list_mutex == NULL) {
        fatal("Failed to create PVDX task list mutex");
    }
    if (task_list[0] != p_watchdog_task) {
        fatal("Watchdog is not first in task_list!");
    }

    // Initialize all OS integrity tasks
    for (pvdx_task_t **curr_task = task_list; *curr_task != NULL; curr_task++) {
        if ((*curr_task)->task_type == OS) {
            init_task_pointer(*curr_task);
            info("%s initialized\n", (*curr_task)->name);
        }
    }

    /* ---------- COSMIC MONKEY TASK ---------- */

// #if defined(UNITTEST) || defined(DEVBUILD)
//     #if defined(UNITTEST)
//     cm_args.frequency = 10;
//     #endif
//     #if defined(DEVBUILD)
//     cm_args.frequency = 1; // Bitflips per second
//     #endif

//     TaskHandle_t cosmic_monkey_task_handle =
//         xTaskCreateStatic(main_cosmic_monkey, "CosmicMonkey", COSMIC_MONKEY_TASK_STACK_SIZE, (void *)&cm_args, 1,
//                           cosmic_monkey_mem.cosmic_monkey_task_stack, &cosmic_monkey_mem.cosmic_monkey_task_tcb);
//     if (cosmic_monkey_task_handle == NULL) {
//         warning("Cosmic Monkey Task Creation Failed!\n");
//     } else {
//         info("Cosmic Monkey Task initialized\n");
//     }
// #endif // Cosmic Monkey

    /* ---------- START FREERTOS SCHEDULER ---------- */

    // Start the scheduler
    vTaskStartScheduler();
    fatal("vTaskStartScheduler Returned! -- Should never happen!\n");
}
