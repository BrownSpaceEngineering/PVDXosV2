/** Run one bootloader integrity scan each time the state machine enables us. */
#include "logging.h"
#include "reflash_task.h"
#include "task_list.h"

void main_reflash_task(void *pvParameters) {
    (void)pvParameters;
    pvdx_task_t *const current_task = get_current_task();
    const TickType_t watchdog_wait = pdMS_TO_TICKS(current_task->watchdog_timeout_ms / 2);

    while (true) {
        // prepare_resume supplies one activation. Waiting here prevents another
        // scan while completion is being processed, without self-suspend races.
        if (ulTaskNotifyTake(pdTRUE, watchdog_wait) != 0) {
            const status_t status = reflash_bootloaders();
            if (status != SUCCESS) {
                warning("reflash: Scan finished with repair errors (status %d)\n", status);
            }
            reflash_report_completion();
        }
        reflash_checkin();
    }
}
