#include "cosmicmonkey_task.h"
#include "atmel_start.h"
#include "logging.h"

struct cosmicmonkeyTaskMemory cosmicmonkeyMem;

static const int VALID_MEMORY_RANGE_START = 0x20000000;
static const int VALID_MEMORY_RANGE_IN_BYTES = 18;
static const int RAND_THREE_BIT_MASK = 0x1c0000;
static const int EIGHTEEN_BIT_MASK = 0x3FFFF;

status_t perform_flip() {
    /* Generate random number */
    uint32_t rand_int = rand_sync_read32(&RAND_0); // Mask the first 21 bits
    uintptr_t p_memory_addr = VALID_MEMORY_RANGE_START + (rand_int & EIGHTEEN_BIT_MASK); //Isolate 18 bits of randomness to pick a random memory address
    int bit_position = (rand_int & RAND_THREE_BIT_MASK) >> VALID_MEMORY_RANGE_IN_BYTES; //Pick the next 3 bits as the index
    if (8 <= bit_position) {
        return ERROR_INTERNAL;
    }
    uint8_t byte_flip_mask = 1 << bit_position; // Generates the a mask of the form 0 ... 010 ... 0

    char* addr = (char*) p_memory_addr; // Gets pointer to the byte that will be modified

    *addr = *addr ^ byte_flip_mask; // Apply the byte_mask with an XOR to the selected byte
    
    return SUCCESS;
}

void cosmicmonkey_main(void *pvParameters) {
    cosmicmonkeyTaskArguments_t args =
        *((cosmicmonkeyTaskArguments_t*) pvParameters);
    
    rand_sync_init(&RAND_0, TRNG); // Initialize random number generator
    rand_sync_enable(&RAND_0); // Enable the random number generator clock

    if (args.frequency == 0) {
        warning("Cosmic Monkey task started, but frequency is 0Hz! Suspending task forever...\n");
        vTaskSuspend(NULL);
    }

    info("Cosmic monkey started with frequency: %d Hz\n", args.frequency);
    while (1)
    {
        if (perform_flip() != SUCCESS) {
            warning("Internal error occured\n");
        }
        int time_task = (1000 / args.frequency); // Generates the time delay in milliseconds based on the frequency
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}