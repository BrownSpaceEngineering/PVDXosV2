/**
 * cosmic_monkey_task.c
 * 
 * Helper functions for the Cosmic Monkey task. This is an antagonist task loosely inspired by
 * Netflix's Chaos Monkey. It randomly flips bits in memory to create an environment akin to low-earth 
 * orbit in order to test the robustness of OS logic to cosmic radiation.
 * 
 * Created: October 6, 2024
 * Authors: Oren Kohavi, Ignacio Blancas Rodriguez
 */

#include "atmel_start.h"
#include "cosmic_monkey_task.h"
#include "logging.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// NOTE: The Cosmic Monkey task is an antagonist task that won't be run in release builds. It has no
// command queue, nor will it be able to receive commands from the command dispatcher task.

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

static const int VALID_MEMORY_RANGE_START = 0x20000000;
static const int VALID_MEMORY_RANGE_IN_BYTES = 18;
static const int RAND_THREE_BIT_MASK = 0x1c0000;
static const int EIGHTEEN_BIT_MASK = 0x3FFFF;

/**
 * \fn perform_flip()
 * 
 * \brief randomly selects a bit in memory and flips it
 * 
 * \warning only used for testing; this function should never be called!
 */
status_t perform_flip() {
    // Implement Cosmic Monkey here! 
    // You should only change bits at or after VALID_MEMORY_RANGE_START
    // You can get a random number using the following line:
    uint32_t rand_int = rand_sync_read32(&RAND_0); // Provides a 21-bit random number

    // Now, using your Cosmic Monkey experience, flip a bit at a random position starting 
    // at VALID_MEMORY_RANGE_START and within a range of (2^18-1)
    
    // Hint: you have 21 bits of randomness to work with. 
    // How many bits do you need to randomize the bit position in a byte?
    // How many bits do you need to randomize the index of the address after VALID_MEMORY_RANGE_START?

    // uintptr_t p_memory_addr = ??
    // continue ...
    
    warning("cosmic_monkey: Flipped bit at address 0x%08x\n", p_memory_addr);
    return SUCCESS;


    // Once you're done...
    // 1. Look at the cosmic_monkey_main.c and cosmic_monkey_task.h files. Do you understand what's happening here? Clarify any confusion with FSW!
    // 2. Figure out how the Cosmic Monkey task is scheduled. Where do we call it from? Where do we set the 'frequency'?
    // 3. Set the devbuild frequency to 20 and watch the OS corrupt and reboot itself repeatedly...
}
