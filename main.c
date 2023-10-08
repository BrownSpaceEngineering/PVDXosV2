#include <atmel_start.h>
#include <hal_adc_sync.h>
#include <utils.h>
#include <driver_init.h>

uint8_t arr[100];
/*
 * ADC_0
 * - imported from driver_init.h, an adc_sync_descriptor
*/
/*
 * type Adc and basically all registers are defined in adc.h
 */
int main(void)
{
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();

    // Initialize ADC

    adc_sync_init(&ADC_0, ADC0, NULL);

	const uint8_t reading_size = 1;

	adc_sync_enable_channel(&ADC_0,0);
	int counter = 0;
	adc_sync_set_resolution(&ADC_0, 8); //8 for 8-bit
    /* Replace with your application code */

	while (1) {
		adc_sync_read_channel(&ADC_0,0, &arr[counter],reading_size);
        // Collect ADC readings and store them
		counter += 1;
        delay_ms(500); //500ms
    }
}



