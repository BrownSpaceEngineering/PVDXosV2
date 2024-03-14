// For now, just a minimal bootloader that checks nothing and defers to the main application.
// In order to check that it worked, increments some counter in backup RAM and then jumps to the main application.
#include "bootloader.h"

#include <stdint.h>

int main() {
    // Increment the counter in backup RAM
    uint32_t *magic_number = (uint32_t *)MAGIC_NUMBER_ADDRESS;
    *magic_number = MAGIC_NUMBER;

    // Set reset vector to the application start address
    uint32_t *reset_vector = (uint32_t *)SCB_VTOR;
    *reset_vector = APPLICATION_START_ADDRESS;

    // Reset the system by writing to the AIRCR register's SYSRESETREQ bit
    uint32_t *reset_control_register = (uint32_t *)SCB_AIRCR;
    *reset_control_register |= AIRCR_SYSRESETREQ;

    // The system should now reset, and the main application should start
    // If not, we're cooked

    // TODO: Maybe add some LED blinking pattern here or something idk

    // If we get here, something went wrong
    *magic_number = 0xBADC0DE; // Write some failure message to the backup RAM
    while (1) {}
}