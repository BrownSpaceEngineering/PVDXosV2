#include <atmel_start.h>
#include <hal_adc_sync.h>
#include <utils.h>
#include <driver_init.h>
#include <stdint.h>
#include <stdarg.h>

#define printf(...) SEGGER_RTT_printf(0, __VA_ARGS__)

extern int SEGGER_RTT_printf(unsigned BufferIndex, const char * sFormat, ...);

void janky_delay(int time){
    for(int i = 0; i < time; i++){
        volatile uint8_t t = 1;
        while(t != 0){
            t++;
        }
    }
}

/*
 * ADC_0
 * - imported from driver_init.h, an adc_sync_descriptor
*/
/*
 * type Adc and basically all registers are defined in adc.h
 */
/*
     REG_PORT_DIR1 |= (1 << 1); //Set direction of PB01 to output
    
    while (1) {
        //Turn on LED
        //Alias to 0x41008090
        REG_PORT_OUT1 |= (1 << 1);
        //Delay 100ms
        janky_delay();
        //Turn off LED
        REG_PORT_OUT1 &= ~(1 << 1);
        //Delay 100ms
		//delay_ms(100);
        janky_delay();
        
    }
 */

int main(void)
{
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    printf("ATMEL Initialization Complete!\n");
    
    //Blinks the red LED a few times (speeding up), just to show signs of life:
    volatile PortGroup* volatile p_portB;
    //p_portB = (PortGroup*)REG_PORT_DIR1;
    p_portB = (PortGroup*)0x41008080UL;
    
    printf("p_portB has an address of: %p\n", p_portB);
    printf("p_portB->DIR has an address of: %p\n", &p_portB->DIR);
    printf("The value of REG_PORT_DIR1 is: %lx\n", REG_PORT_DIR1);
    
    //Set PB01 direction to output:
    p_portB->DIR.reg |= (1 << 1);
    //REG_PORT_DIR1 |= (1 << 1); //Set direction of PB01 to output
    for(volatile int i = 250; i > 0; i = i - 10) {
        //Set output high
        p_portB->OUT.reg |= (1 << 1);
        //REG_PORT_OUT1 |= (1 << 1);
        janky_delay(i);
        //Set output low
        p_portB->OUT.reg &= ~(1 << 1);
        //REG_PORT_OUT1 &= ~(1 << 1);
        janky_delay(i);
    }
    //Set output high forever
    p_portB->OUT.reg |= (1 << 1);
    
    printf("Work completed -- Looping forever\n");
    while(1) {
        
    }
    /*
    adc_sync_init(&ADC_0, ADC0, NULL);

	const uint8_t reading_size = 1;

	adc_sync_enable_channel(&ADC_0,0);
	int counter = 0;
	adc_sync_set_resolution(&ADC_0, 8); //8 for 8-bit

	while (1) {
		adc_sync_read_channel(&ADC_0,0, &arr[counter],reading_size);
        // Collect ADC readings and store them
		counter += 1;
        delay_ms(500); //500ms
    }
     */
}



