#include "default_handler.h"

void PVDX_default_handler() {
    // Figure out which interrupt brought us here
    SCB_Type *scb = SCB;
    uint32_t ICSR_value = scb->ICSR;
    uint32_t vectactive = ICSR_value & SCB_ICSR_VECTACTIVE_Msk; // Isolate the bits that contain the fault reason

    warning("Default Handler Triggered! (Vector Active: %d)\n", vectactive);

    if (vectactive > 0xf) {}

    switch (vectactive) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            // These are unrecoverable faults! Log this and then reset the system
            fatal("Unrecoverable fault! (Vector Active: %d)\n", vectactive);
            break;
        case 7:
        case 8:
        case 9:
        case 10:
            // These are reserved and should never happen
            fatal("Reserved fault! (Vector Active: %d)\n", vectactive);
            break;
        case 11:
            // SVCall [basically a syscall], which should be handled by RTOS. If we get here there is a problem
            fatal("SVCall fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
        case 12:
        case 13:
            // Reserved and should never happen
            fatal("Reserved fault! (Vector Active: %d)\n", vectactive);
            break;
        case 14:
        case 15:
            // PendSV and SysTick are handled by the RTOS. If we get here there is a problem
            fatal("PendSV or SysTick fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
        case 16:
            // 16 is the interrupt number for all IRQs, so it's not that serious and can be recovered from
            warning("IRQ fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
        default:
            // I don't think vectactive can go above 16, but if it does, log it and treat it the same as 16
            warning("Unknown fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
    }

    // Past the switch statement, this means we didn't reset the system.
    // Try to recover from the interrupt

    // Clear the fault
}