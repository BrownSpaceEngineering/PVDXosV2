/**
 * heartbeat_main.c
 *
 * Main loop of the Heartbeat task, which simply blinks an LED at a fixed rate to indicate that the
 * system is still running.
 *
 * Created: November 20, 2023
 * Authors: Oren Kohavi, Tanish Makadia
 */

#include "logging.h"
#include "heartbeat_task.h"
#include "cmd_dispatcher.h"
#include "state_machine.h"

heartbeat_task_memory_t heartbeat_mem                                                                ;

/* ---------- EXAMPLE ONLY: MOCKED STATE MACHINE INPUTS ----------
 * The heartbeat task only runs in STATE_FAKE_HEARTBEAT. While it runs, it mocks the inputs that nothing produces yet,
 * alternating between two fake orbit phases, so that IDLE goes on to one activity afterwards:
 *   - eclipse:  out of sunlight, with a new student picture to show -> STUDENT_PICTURE_CAPTURE
 *   - sunlight: in sunlight, last perovskite sweep "downlinked"    -> PEROVSKITE_CAPTURE
 * TODO: remove once ADCS reports sunlight, uplink reports new pictures, and downlink clears the ready flags.
 */

// At most one mock update per MOCK_INPUT_PERIOD_MS. Longer than a FAKE_HEARTBEAT visit (5 s) and shorter than the
// time between visits (about 30 s), so each visit sends exactly one. Measured in ticks rather than with vTaskDelay(),
// because suspending this task between visits cuts any delay short.
#define MOCK_INPUT_PERIOD_MS 20000
static input_update_data_t mock_eclipse_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(SUNLIGHT_STATUS_UPDATE) | INPUT_UPDATE_BIT(IMAGE_READY_DISPLAY_UPDATE) |
                            INPUT_UPDATE_BIT(PHOTO_READY_DOWNLINK_UPDATE),
    .new_values = {.in_sunlight = false, .image_ready_to_display = true, .photo_taken_for_downlink = false},
};

static input_update_data_t mock_sunlight_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(SUNLIGHT_STATUS_UPDATE) | INPUT_UPDATE_BIT(PEROVSKITE_READY_DOWNLINK_UPDATE),
    .new_values = {.in_sunlight = true, .perovskite_data_ready_for_downlink = false},
};

static bool next_is_sunlight = true; // Start with the perovskite sweep, then the student picture
static bool mock_sent_before = false;
static TickType_t last_mock_tick = 0;

// Sends the next phase's mock inputs, unless one was sent in the last MOCK_INPUT_PERIOD_MS
static void send_mock_inputs_if_due(void) {
    const TickType_t now = xTaskGetTickCount();
    if (mock_sent_before && now - last_mock_tick < pdMS_TO_TICKS(MOCK_INPUT_PERIOD_MS)) {
        return;
    }

    command_t cmd = get_state_input_update_command(next_is_sunlight ? &mock_sunlight_inputs : &mock_eclipse_inputs);
    if (enqueue_command(&cmd) != SUCCESS) {
        warning("heartbeat: Failed to enqueue mock input update, retrying\n");
        return; // tried again next loop
    }
    info("heartbeat: mocking %s\n", next_is_sunlight ? "sunlight (perovskite sweep next)" : "eclipse (student picture next)");
    next_is_sunlight = !next_is_sunlight;
    mock_sent_before = true;
    last_mock_tick = now;
}

/**
 * \fn main_heartbeat
 *
 * \param pvParameters a void pointer to the parametres required by the
 *      heartbeat task; not currently set by config
 *
 * \warning should never return
 */
void main_heartbeat(void *pvParameters)                                                              {
    info("heartbeat: Task Started!\n")                                                               ;

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task()                                             ;
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task)                               ;

// NOTE: false is on for some reason on the orange LEDs
// In release build, make sure orange LEDs are off
#if defined(RELEASE)
    gpio_set_pin_level(LED_ORANGE1, true)                                                            ;
    gpio_set_pin_level(LED_ORANGE2, true)                                                            ;
#endif

    while (true)                                                                                     {
        debug("\n---------- Heartbeat Task Loop ----------\n")                                       ;

        // Print the current time
        debug("heartbeat: Current time is %d\n", xTaskGetTickCount())                                ;

        send_mock_inputs_if_due();

// Devbuild heartbeat pattern (Smoothly turning on and off LEDs in a line)
#if defined(DEVBUILD)
        gpio_set_pin_level(LED_ORANGE1, false)                                                       ;
        vTaskDelay(pdMS_TO_TICKS(100))                                                               ;
        gpio_set_pin_level(LED_ORANGE2, false)                                                       ;
        vTaskDelay(pdMS_TO_TICKS(100))                                                               ;
        gpio_set_pin_level(LED_RED, true)                                                            ;
        vTaskDelay(pdMS_TO_TICKS(400))                                                               ;
        gpio_set_pin_level(LED_ORANGE1, true)                                                        ;
        vTaskDelay(pdMS_TO_TICKS(33))                                                                ;
        gpio_set_pin_level(LED_ORANGE2, true)                                                        ;
        vTaskDelay(pdMS_TO_TICKS(33))                                                                ;
        gpio_set_pin_level(LED_RED, false)                                                           ;
        vTaskDelay(pdMS_TO_TICKS(300))                                                               ;
#endif

// Testing heartbeat pattern (Blinking LEDs in an oscillating pattern [ON/OFF/ON] -> [OFF/ON/OFF])
#if defined(UNITTEST)
        gpio_set_pin_level(LED_ORANGE1, false)                                                       ;
        gpio_set_pin_level(LED_ORANGE2, true)                                                        ;
        gpio_set_pin_level(LED_RED, true)                                                            ;
        vTaskDelay(pdMS_TO_TICKS(500))                                                               ;
        gpio_set_pin_level(LED_ORANGE1, true)                                                        ;
        gpio_set_pin_level(LED_ORANGE2, false)                                                       ;
        gpio_set_pin_level(LED_RED, false)                                                           ;
        vTaskDelay(pdMS_TO_TICKS(500))                                                               ;
#endif

// Release heartbeat pattern (Red LED only, blinking at 1Hz)
#if defined(RELEASE)
        gpio_set_pin_level(LED_RED, true)                                                            ;
        vTaskDelay(pdMS_TO_TICKS(500))                                                               ;
        gpio_set_pin_level(LED_RED, false)                                                           ;
        vTaskDelay(pdMS_TO_TICKS(500))                                                               ;
#endif

        // Check in with the watchdog task
        if (should_checkin(current_task))                                                            {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("heartbeat: Failed to enqueue watchdog checkin command\n");
            }
            debug("heartbeat: Enqueued watchdog checkin command\n")                                  ;}}}
