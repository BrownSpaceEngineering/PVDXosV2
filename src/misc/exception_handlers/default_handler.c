#include "default_handler.h"

#include "globals.h"
#include "logging.h"

#define IRQ_VECTOR_OFFSET 16

// Array of fault names for easy lookup
const char *FAULT_NAMES[] = {"Thread mode", "Reserved", "NMI",      "HardFault", "MemManage",          "BusFault", "UsageFault", "Reserved",
                             "Reserved",    "Reserved", "Reserved", "SVCall",    "Reserved for Debug", "Reserved", "PendSV",     "SysTick"};

void PVDX_default_handler() {
    // Figure out which interrupt brought us here
    SCB_Type *scb = SCB;
    uint32_t ICSR_value = scb->ICSR;
    uint32_t vectactive = ICSR_value & SCB_ICSR_VECTACTIVE_Msk; // Isolate the bits that contain the fault reason

    // If the fault reason is 16 or higher, this is actually just an IRQ interrupt
    if (vectactive >= IRQ_VECTOR_OFFSET) {
        // This is an IRQ interrupt, not a fault
        warning("Default Handler triggered by IRQ interrupt! [IRQ Number: %d]\n", vectactive - IRQ_VECTOR_OFFSET);
        // Try to hopefully recover from this fault (TODO: Not sure if return actually works in this case, might be more complex)
        return;
    }

    /* -- If we get here, then we have a hardware exception/fault of some sort -- */

    // Read CPU registers that give more detail on the fault that just happened
    uintptr_t bus_faulty_address = scb->BFAR;
    uintptr_t mem_manage_faulty_address = scb->MMFAR;
    uint32_t hard_fault_sr = scb->HFSR;
    uint32_t configurable_fault_sr = scb->CFSR;
    uint16_t usage_fault_sr = (configurable_fault_sr & SCB_CFSR_USGFAULTSR_Msk) >> SCB_CFSR_USGFAULTSR_Pos;
    uint8_t bus_fault_sr = (configurable_fault_sr & SCB_CFSR_BUSFAULTSR_Msk) >> SCB_CFSR_BUSFAULTSR_Pos;
    uint8_t mem_manage_fault_sr = (configurable_fault_sr & SCB_CFSR_MEMFAULTSR_Msk) >> SCB_CFSR_MEMFAULTSR_Pos;

    // Break down the CFSR subfields into individual bits
    // ### Usage Fault Status Register (UFSR)
    bool div_by_zero_fault = (usage_fault_sr & SCB_CFSR_DIVBYZERO_Msk) >> SCB_CFSR_DIVBYZERO_Pos;
    bool unaligned_access_fault = (usage_fault_sr & SCB_CFSR_UNALIGNED_Msk) >> SCB_CFSR_UNALIGNED_Pos;
    bool no_coprocessor_fault = (usage_fault_sr & SCB_CFSR_NOCP_Msk) >> SCB_CFSR_NOCP_Pos;
    bool invalid_pc_fault = (usage_fault_sr & SCB_CFSR_INVPC_Msk) >> SCB_CFSR_INVPC_Pos;
    bool invalid_state_fault = (usage_fault_sr & SCB_CFSR_INVSTATE_Msk) >> SCB_CFSR_INVSTATE_Pos;
    bool undefined_instruction_fault = (usage_fault_sr & SCB_CFSR_UNDEFINSTR_Msk) >> SCB_CFSR_UNDEFINSTR_Pos;

    // ### Bus Fault Status Register (BFSR)
    bool bfsr_valid = (bus_fault_sr & SCB_CFSR_BFARVALID_Msk) >> SCB_CFSR_BFARVALID_Pos;
    /*
    // --- These are all valid fields, but we don't need them for now ---
    bool bus_fault_floatlazy_state_preservation = (bus_fault_sr & SCB_CFSR_LSPERR_Msk) >> SCB_CFSR_LSPERR_Pos;
    bool bus_fault_stacking_error = (bus_fault_sr & SCB_CFSR_STKERR_Msk) >> SCB_CFSR_STKERR_Pos;
    bool bus_fault_unstacking_error = (bus_fault_sr & SCB_CFSR_UNSTKERR_Msk) >> SCB_CFSR_UNSTKERR_Pos;
    bool bus_fault_imprecise_error = (bus_fault_sr & SCB_CFSR_IMPRECISERR_Msk) >> SCB_CFSR_IMPRECISERR_Pos;
    bool bus_fault_precise_error = (bus_fault_sr & SCB_CFSR_PRECISERR_Msk) >> SCB_CFSR_PRECISERR_Pos;
    bool bus_fault_instruction_error = (bus_fault_sr & SCB_CFSR_IBUSERR_Msk) >> SCB_CFSR_IBUSERR_Pos;
    */

    // ### Memory Manage Fault Status Register (MMSR)
    bool mmar_valid = (mem_manage_fault_sr & SCB_CFSR_MMARVALID_Msk) >> SCB_CFSR_MMARVALID_Pos;
    bool memmang_fault_floatlazy_state_preservation = (mem_manage_fault_sr & SCB_CFSR_MLSPERR_Msk) >> SCB_CFSR_MLSPERR_Pos;
    bool memmang_fault_stacking_error = (mem_manage_fault_sr & SCB_CFSR_MSTKERR_Msk) >> SCB_CFSR_MSTKERR_Pos;
    bool memmang_fault_unstacking_error = (mem_manage_fault_sr & SCB_CFSR_MUNSTKERR_Msk) >> SCB_CFSR_MUNSTKERR_Pos;
    bool memmang_fault_data_violation = (mem_manage_fault_sr & SCB_CFSR_DACCVIOL_Msk) >> SCB_CFSR_DACCVIOL_Pos;
    bool memmang_fault_instruction_violation = (mem_manage_fault_sr & SCB_CFSR_IACCVIOL_Msk) >> SCB_CFSR_IACCVIOL_Pos;

    // ### Hard Fault Status Register (HFSR)
    bool forced_hard_fault = (hard_fault_sr & SCB_HFSR_FORCED_Msk) >> SCB_HFSR_FORCED_Pos;
    bool vector_table_hard_fault = (hard_fault_sr & SCB_HFSR_VECTTBL_Msk) >> SCB_HFSR_VECTTBL_Pos;

    warning("HARDWARE FAULT -- Default Handler Triggered! (Fault Number: %d)\n", vectactive);
    warning("Fault Type: %s\n", FAULT_NAMES[vectactive]);
    warning_impl("Detailed Fault Information:\n");

    if (bfsr_valid) {
        warning_impl("\t- Bus Fault Address: 0x%08X\n", bus_faulty_address);
    } else {
        warning_impl("\t- Bus Fault Address: N/A\n");
    }

    if (mmar_valid) {
        warning_impl("\t- Memory Manage Fault Address: 0x%08X\n", mem_manage_faulty_address);
    } else {
        warning_impl("\t- Memory Manage Fault Address: N/A\n");
    }

    warning_impl("\t- Usage Fault from Division by Zero: %s\n", div_by_zero_fault ? "Yes" : "No");
    warning_impl("\t- Usage Fault from Unaligned Access: %s\n", unaligned_access_fault ? "Yes" : "No");
    warning_impl("\t- Usage Fault from No Coprocessor: %s\n", no_coprocessor_fault ? "Yes" : "No");
    warning_impl("\t- Usage Fault from Invalid PC Load: %s\n", invalid_pc_fault ? "Yes" : "No");
    warning_impl("\t- Usage Fault from Invalid State: %s\n", invalid_state_fault ? "Yes" : "No");
    warning_impl("\t- Usage Fault from Undefined Instruction: %s\n", undefined_instruction_fault ? "Yes" : "No");

    /*
    // --- Not sure if we want this level of detail ---
    warning_impl("\t- Memory Manage Fault from Float/Lazy State Preservation: %s\n",
                 memmang_fault_floatlazy_state_preservation ? "Yes" : "No");
    warning_impl("\t- Memory Manage Fault from Stacking Error: %s\n", memmang_fault_stacking_error ? "Yes" : "No");
    warning_impl("\t- Memory Manage Fault from Unstacking Error: %s\n", memmang_fault_unstacking_error ? "Yes" : "No");
    */
    warning_impl("\t- Memory Manage Fault from Data Access Violation: %s\n", memmang_fault_data_violation ? "Yes" : "No");
    warning_impl("\t- Memory Manage Fault from Instruction Access Violation: %s\n", memmang_fault_instruction_violation ? "Yes" : "No");
    warning_impl("\t- Fault was forced (escalated from lower-tier fault to hardfault): %s\n", forced_hard_fault ? "Yes" : "No");
    warning_impl("\t- Fault occurred in vector table read: %s\n", vector_table_hard_fault ? "Yes" : "No");

    warning_impl(" ---------- \n");

    switch (vectactive) {
        case 1: // Reserved, should not occur in normal use
        case 2: // Non-Maskable Interrupt
        case 3: // Hard fault (i.e. invalid memory access, illegal instruction, division by zero, etc.)
        case 4: // MemManage
        case 5: // Bus Fault
        case 6: // Usage Fault
            // These are unrecoverable faults! Log this and then reset the system
            fatal("Unrecoverable fault! (Vector Active: %d)\n", vectactive);
            break;
        case 7:
        case 8:
        case 9:
        case 10:
            // These are reserved and should never happen
            fatal("Reserved fault SHOULD NEVER HAPPEN! (Vector Active: %d)\n", vectactive);
            break;
        case 11:
            // SVCall [basically a syscall], which should be handled by RTOS. If we get here there is a problem
            fatal("SVCall fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
        case 12:
        case 13:
            // Reserved and should never happen
            fatal("Reserved fault SHOULD NEVER HAPPEN! (Vector Active: %d)\n", vectactive);
            break;
        case 14: // PendSV (pending syscall)
        case 15: // SysTick (tick increment for RTOS)
            // PendSV and SysTick are handled by the RTOS. If we get here there is a problem
            fatal("PendSV or SysTick fault went into PVDX handler (Vector Active: %d)\n", vectactive);
            break;
        default:
            fatal("Reached default case in exception handling switch statement! (Vector Active: %d)\n", vectactive);
            break;
    }
    fatal("SHOULD NEVER GET HERE\n");
    __builtin_unreachable();
}