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

struct spi_m_sync_descriptor SPI_MRAM;

void SPI_MRAM_PORT_init(void)
{

	gpio_set_pin_level(MRAM_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM_MOSI, PINMUX_PB16C_SERCOM5_PAD0);

	gpio_set_pin_level(MRAM_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM_SCK, PINMUX_PB17C_SERCOM5_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(MRAM_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(MRAM_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MRAM_MISO, PINMUX_PB18C_SERCOM5_PAD2);
}

void SPI_MRAM_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_CORE, CONF_GCLK_SERCOM5_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_SLOW, CONF_GCLK_SERCOM5_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM5_bit(MCLK);
}

void SPI_MRAM_init(void)
{
	SPI_MRAM_CLOCK_init();
	spi_m_sync_init(&SPI_MRAM, SERCOM5);
	SPI_MRAM_PORT_init();
}

void system_init(void)
{
	init_mcu();

	// GPIO on PC04

	gpio_set_pin_level(MRAM1_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM1_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM1_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC05

	gpio_set_pin_level(MRAM1_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM1_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM1_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC06

	gpio_set_pin_level(MRAM2_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC07

	gpio_set_pin_level(MRAM2_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC10

	gpio_set_pin_level(MRAM3_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC11

	gpio_set_pin_level(MRAM3_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC12

	gpio_set_pin_level(MRAM3_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_WP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC14

	gpio_set_pin_level(MRAM1_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM1_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM1_WP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC15

	gpio_set_pin_level(MRAM2_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_WP, GPIO_PIN_FUNCTION_OFF);

	SPI_MRAM_init();
}
