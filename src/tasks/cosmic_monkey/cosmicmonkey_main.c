#include "cosmicmonkey_task.h"
#include "atmel_start.h"
#include "SEGGER_RTT_printf.h"

struct cosmicmonkeyTaskMemory cosmicmonkeyMem;

static const int VALID_MEMORY_RANGE_START = 0x20000000;
static const int VALID_MEMORY_RANGE_IN_BYTES = 18;
static const int RAND_THREE_BIT_MASK = 0x1c0000;
static const int EIGHTEEN_BIT_MASK = 0x3FFFF;

void perform_flip()
{
    printf("Function called\r\n");
    /* Generate random number */
    uint32_t rand_int = rand_sync_read32(&RAND_0); // Mask the first 21 bits
    printf("Past the rand_int function, result: %u\r\n", rand_int);
    uintptr_t p_memory_addr = VALID_MEMORY_RANGE_START + (rand_int & EIGHTEEN_BIT_MASK); //Isolate 18 bits of randomness to pick a random memory address
    int bit_position = (rand_int & RAND_THREE_BIT_MASK) >> VALID_MEMORY_RANGE_IN_BYTES; //Pick the next 3 bits as the index
    if (8 <= bit_position){
        printf("Unexpected value for bit position");
    }
    uint8_t byte_flip_mask = 1 << bit_position;

    printf("Calculated bitmask: %d \r\n", byte_flip_mask);
    char* addr = (char*) p_memory_addr;
    printf("%02X \r\n", *addr);

    *addr = *addr ^ byte_flip_mask; // Apply the byte_mask with an XOR to the selected byte
    
    printf("%02X \r\n", *addr);
}

void cosmicmonkey_main(void *pvParameters)
{
    struct cosmicmonkeyTaskArguments args = 
        *((struct cosmicmonkeyTaskArguments *) pvParameters);
    
    printf("Gone through here\r\n");
    rand_sync_init(&RAND_0, TRNG); // Initialize random number generator
    rand_sync_enable(&RAND_0); // Enable the random number generator clock

    while (1)
    {
        perform_flip();
        int time_task = (1000 / args.frequency);
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}