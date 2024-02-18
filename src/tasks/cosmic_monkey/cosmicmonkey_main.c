//#ifdef UNITTEST
#include "atmel_start.h"
#include "SEGGER_RTT_printf.h"

int FREQUENCY = 10;

void perform_flip()
{
    /* Generate random number */
    uint32_t rand_int = rand_sync_read32(&RAND_0) & 0x3FFFFF; // Mask the first 21 bits
    uint8_t rand_bit_in_byte_idx = rand_int >> 18; // Get the first 3 bits of the sequence
    uint32_t rand_byte_idx = rand_int & 0x7FFFF; // Mask the first 18 bits of the random integer

    uint8_t byte_mask = 1; // Mask that will be XORed with the given byte to flip the random bit
    /* Generates the byte mask by powering 2 to the according random 3-bit number */
    for (int i = 0; i < rand_bit_in_byte_idx; i++)
    {
        byte_mask = byte_mask * 2;
    }
    char* addr = (char*) (0x20000000 + rand_byte_idx);
    printf("%c \n", *addr);

    *addr = *addr ^ byte_mask; // Apply the byte_mask with an XOR to the selected byte
    
    printf("%c \n", *addr);
}

void cosmicmonkey_main(void *pvParameters)
{
    while (1)
    {
        perform_flip();
        int time_task = (1000 / FREQUENCY);
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}
//#endif