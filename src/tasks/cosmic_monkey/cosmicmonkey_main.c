#include "cosmicmonkey_task.h"
#include "atmel_start.h"
#include "SEGGER_RTT_printf.h"

struct cosmicmonkeyTaskMemory cosmicmonkeyMem;
struct cosmicmonkeyTaskArguments cosmicmonkeyTaskArgs;


int FREQUENCY = 10;
static const int VALID_MEMORY_RANGE_IN_BYTES = 18;

void perform_flip()
{
    printf("Function called\r\n");
    /* Generate random number */
    uint32_t rand_int = rand_sync_read32(&RAND_0); // Mask the first 21 bits
    printf("Past the rand_int function, result: %u\r\n", rand_int);
    uintptr_t memory_addr = 0x20000000 + (rand_int & 0x3FFFF); //Isolate 18 bits of randomness to pick a random memory address
    int bit_position = (rand_int & 0x1c0000) >> VALID_MEMORY_RANGE_IN_BYTES; //Pick the next 3 bits as the index
    if (bit_position >= 8){
        printf("Unexpected value for bit position");
    }
    uint8_t byte_flip_mask = 1 << bit_position;

    printf("Calculated bitmask: %d \r\n", byte_flip_mask);
    char* addr = (char*) memory_addr;
    printf("%02X \r\n", *addr);

    *addr = *addr ^ byte_flip_mask; // Apply the byte_mask with an XOR to the selected byte
    
    printf("%02X \r\n", *addr);
}

void cosmicmonkey_main(void *pvParameters)
{
    struct cosmicmonkeyTaskArgs args =
        (struct cosmicmonkeyTaskArgs) (*pvParameters);
    
    printf("Gone through here\r\n");
    rand_sync_init(&RAND_0, TRNG);
    rand_sync_enable(&RAND_0);
    while (1)
    {
        perform_flip();
        int time_task = (1000 / args.frequency);
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}