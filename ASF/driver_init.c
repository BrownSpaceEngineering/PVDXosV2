/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */

#include "driver_init.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>

/* The priority of the peripheral should be between the low and high interrupt priority set by chosen RTOS,
 * Otherwise, some of the RTOS APIs may fail to work inside interrupts
 * In case of FreeRTOS,the Lowest Interrupt priority is set by configLIBRARY_LOWEST_INTERRUPT_PRIORITY and
 * Maximum interrupt priority by configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, So Interrupt Priority of the peripheral
 * should be between configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY and configLIBRARY_LOWEST_INTERRUPT_PRIORITY
 */
#define PERIPHERAL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY - 1)

#include <hpl_adc_base.h>
#include <hpl_adc_base.h>

struct adc_sync_descriptor ADC_0;

struct adc_sync_descriptor ADC_1;

struct usart_sync_descriptor USART_0;

struct i2c_m_sync_desc I2C_0;

struct spi_m_os_descriptor SPI_0;

struct rand_sync_desc RAND_0;

struct wdt_descriptor WDT_0;

void ADC_0_PORT_init(void)
{
}

void ADC_0_CLOCK_init(void)
{
	hri_mclk_set_APBDMASK_ADC0_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, ADC0_GCLK_ID, CONF_GCLK_ADC0_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void ADC_0_init(void)
{
	ADC_0_CLOCK_init();
	ADC_0_PORT_init();
	adc_sync_init(&ADC_0, ADC0, (void *)NULL);
}

void ADC_1_PORT_init(void)
{
}

void ADC_1_CLOCK_init(void)
{
	hri_mclk_set_APBDMASK_ADC1_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, ADC1_GCLK_ID, CONF_GCLK_ADC1_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void ADC_1_init(void)
{
	ADC_1_CLOCK_init();
	ADC_1_PORT_init();
	adc_sync_init(&ADC_1, ADC1, (void *)NULL);
}

void USART_0_PORT_init(void)
{

	gpio_set_pin_function(PA04, PINMUX_PA04D_SERCOM0_PAD0);

	gpio_set_pin_function(PA05, PINMUX_PA05D_SERCOM0_PAD1);
}

void USART_0_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM0_GCLK_ID_CORE, CONF_GCLK_SERCOM0_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM0_GCLK_ID_SLOW, CONF_GCLK_SERCOM0_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBAMASK_SERCOM0_bit(MCLK);
}

void USART_0_init(void)
{
	USART_0_CLOCK_init();
	usart_sync_init(&USART_0, SERCOM0, (void *)NULL);
	USART_0_PORT_init();
}

void I2C_0_PORT_init(void)
{

	gpio_set_pin_pull_mode(PA16,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(PA16, PINMUX_PA16C_SERCOM1_PAD0);

	gpio_set_pin_pull_mode(PA17,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(PA17, PINMUX_PA17C_SERCOM1_PAD1);
}

void I2C_0_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM1_GCLK_ID_CORE, CONF_GCLK_SERCOM1_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM1_GCLK_ID_SLOW, CONF_GCLK_SERCOM1_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBAMASK_SERCOM1_bit(MCLK);
}

void I2C_0_init(void)
{
	I2C_0_CLOCK_init();
	i2c_m_sync_init(&I2C_0, SERCOM1);
	I2C_0_PORT_init();
}

void SPI_0_PORT_init(void)
{

	gpio_set_pin_level(UHF_COPI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_COPI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_COPI, PINMUX_PC23D_SERCOM3_PAD0);

	gpio_set_pin_level(UHF_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_SCK, PINMUX_PC22D_SERCOM3_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(UHF_CIPO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(UHF_CIPO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(UHF_CIPO, PINMUX_PA18D_SERCOM3_PAD2);
}

void SPI_0_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_CORE, CONF_GCLK_SERCOM3_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_SLOW, CONF_GCLK_SERCOM3_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM3_bit(MCLK);
}

void SPI_0_init(void)
{
	SPI_0_CLOCK_init();
	uint32_t irq = SERCOM3_0_IRQn;
	for (uint32_t i = 0; i < 4; i++) {
		NVIC_SetPriority((IRQn_Type)irq, PERIPHERAL_INTERRUPT_PRIORITY);
		irq++;
	}
	spi_m_os_init(&SPI_0, SERCOM3);
	spi_m_os_enable(&SPI_0);
	SPI_0_PORT_init();
}

void delay_driver_init(void)
{
	delay_init(SysTick);
}

void RAND_0_CLOCK_init(void)
{
	hri_mclk_set_APBCMASK_TRNG_bit(MCLK);
}

void RAND_0_init(void)
{
	RAND_0_CLOCK_init();
	rand_sync_init(&RAND_0, TRNG);
}

void WDT_0_CLOCK_init(void)
{
	hri_mclk_set_APBAMASK_WDT_bit(MCLK);
}

void WDT_0_init(void)
{
	WDT_0_CLOCK_init();
	wdt_init(&WDT_0, WDT);
}

void system_init(void)
{
	init_mcu();

	// GPIO on PA02

	gpio_set_pin_level(UHF_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PA20

	gpio_set_pin_level(BITBANG_COPI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(BITBANG_COPI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(BITBANG_COPI, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PA21

	// Set pin direction to input
	gpio_set_pin_direction(BITBANG_CIPO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(BITBANG_CIPO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(BITBANG_CIPO, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PA22

	gpio_set_pin_level(BITBANG_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(BITBANG_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(BITBANG_SCK, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB01

	gpio_set_pin_level(LED_Red,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(LED_Red, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_Red, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB05

	// Set pin direction to input
	gpio_set_pin_direction(UHF_IRQ, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(UHF_IRQ,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(UHF_IRQ, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB25

	gpio_set_pin_level(UHF_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC30

	gpio_set_pin_level(LED_Orange1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_Orange1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_Orange1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC31

	gpio_set_pin_level(LED_Orange2,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_Orange2, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_Orange2, GPIO_PIN_FUNCTION_OFF);

	ADC_0_init();

	ADC_1_init();

	USART_0_init();

	I2C_0_init();

	SPI_0_init();

	delay_driver_init();

	RAND_0_init();

	WDT_0_init();
}
