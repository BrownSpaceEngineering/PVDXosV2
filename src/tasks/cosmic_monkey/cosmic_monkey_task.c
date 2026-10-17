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
    /* Generate random number */
    uint32_t rand_int = rand_sync_read32(&RAND_0); // Mask the first 21 bits
    uintptr_t p_memory_addr =
        VALID_MEMORY_RANGE_START + (rand_int & EIGHTEEN_BIT_MASK); // Isolate 18 bits of randomness to pick a random memory address
    int bit_position = (rand_int & RAND_THREE_BIT_MASK) >> VALID_MEMORY_RANGE_IN_BYTES; // Pick the next 3 bits as the index
    if (8 <= bit_position) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    uint8_t byte_flip_mask = 1 << bit_position; // Generates the a mask of the form 0 ... 010 ... 0

    char* addr = (char*)p_memory_addr; // Gets pointer to the byte that will be modified

    *addr = *addr ^ byte_flip_mask; // Apply the byte_mask with an XOR to the selected byte

    warning("cosmic_monkey: Flipped bit at address 0x%08x\n", p_memory_addr);
    return SUCCESS;
}
