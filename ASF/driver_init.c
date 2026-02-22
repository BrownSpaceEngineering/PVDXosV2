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

#include <hpl_adc_base.h>

struct spi_m_sync_descriptor SPI_MRAM;
struct spi_m_sync_descriptor SPI_DISPLAY;
struct spi_m_sync_descriptor SPI_CAMERA;

struct adc_sync_descriptor ADC_0;

struct flash_descriptor FLASH_0;

struct i2c_m_sync_desc I2C_SBAND;

struct i2c_m_sync_desc I2C_MAG_GYRO;

struct i2c_m_sync_desc I2C_CAMERA;

struct rand_sync_desc RAND_0;

struct wdt_descriptor WDT_0;

void ADC_0_PORT_init(void)
{

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB08, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB08, PINMUX_PB08B_ADC1_AIN0);
}

void ADC_0_CLOCK_init(void)
{
	hri_mclk_set_APBDMASK_ADC1_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, ADC1_GCLK_ID, CONF_GCLK_ADC1_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void ADC_0_init(void)
{
	ADC_0_CLOCK_init();
	ADC_0_PORT_init();
	adc_sync_init(&ADC_0, ADC1, (void *)NULL);
}

void FLASH_0_CLOCK_init(void)
{

	hri_mclk_set_AHBMASK_NVMCTRL_bit(MCLK);
}

void FLASH_0_init(void)
{
	FLASH_0_CLOCK_init();
	flash_init(&FLASH_0, NVMCTRL);
}

void I2C_SBAND_PORT_init(void)
{

	gpio_set_pin_pull_mode(SBAND_SDA,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(SBAND_SDA, PINMUX_PA09D_SERCOM2_PAD0);

	gpio_set_pin_pull_mode(SBAND_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(SBAND_SCL, PINMUX_PA08D_SERCOM2_PAD1);
}

void I2C_SBAND_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_CORE, CONF_GCLK_SERCOM2_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_SLOW, CONF_GCLK_SERCOM2_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM2_bit(MCLK);
}

void I2C_SBAND_init(void)
{
	I2C_SBAND_CLOCK_init();
	i2c_m_sync_init(&I2C_SBAND, SERCOM2);
	I2C_SBAND_PORT_init();
}

void I2C_MAG_GYRO_PORT_init(void)
{

	gpio_set_pin_pull_mode(Magnetometer_Gyro_SDA,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Magnetometer_Gyro_SDA, PINMUX_PA17D_SERCOM3_PAD0);

	gpio_set_pin_pull_mode(Magnetometer_Gyro_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Magnetometer_Gyro_SCL, PINMUX_PA16D_SERCOM3_PAD1);
}

void I2C_MAG_GYRO_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_CORE, CONF_GCLK_SERCOM3_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_SLOW, CONF_GCLK_SERCOM3_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM3_bit(MCLK);
}

void I2C_MAG_GYRO_init(void)
{
	I2C_MAG_GYRO_CLOCK_init();
	i2c_m_sync_init(&I2C_MAG_GYRO, SERCOM3);
	I2C_MAG_GYRO_PORT_init();
}

void I2C_CAMERA_PORT_init(void)
{

	gpio_set_pin_pull_mode(Camera_SDA,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Camera_SDA, PINMUX_PA13D_SERCOM4_PAD0);

	gpio_set_pin_pull_mode(Camera_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Camera_SCL, PINMUX_PA12D_SERCOM4_PAD1);
}

void I2C_CAMERA_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_SLOW, CONF_GCLK_SERCOM4_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM4_bit(MCLK);
}

void I2C_CAMERA_init(void)
{
	I2C_CAMERA_CLOCK_init();
	i2c_m_sync_init(&I2C_CAMERA, SERCOM4);
	I2C_CAMERA_PORT_init();
}

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

void SPI_DISPLAY_PORT_init(void)
{

	gpio_set_pin_level(Display_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(Display_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Display_MOSI, PINMUX_PD09D_SERCOM6_PAD0);

	gpio_set_pin_level(Display_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(Display_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Display_SCK, PINMUX_PD08D_SERCOM6_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(Display_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(Display_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Display_MISO, PINMUX_PD10D_SERCOM6_PAD2);
}

void SPI_DISPLAY_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM6_GCLK_ID_CORE, CONF_GCLK_SERCOM6_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM6_GCLK_ID_SLOW, CONF_GCLK_SERCOM6_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM6_bit(MCLK);
}

void SPI_DISPLAY_init(void)
{
	SPI_DISPLAY_CLOCK_init();
	spi_m_sync_init(&SPI_DISPLAY, SERCOM6);
	SPI_DISPLAY_PORT_init();
}

void SPI_CAMERA_PORT_init(void)
{

	gpio_set_pin_level(Camera_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(Camera_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Camera_MOSI, PINMUX_PB30C_SERCOM7_PAD0);

	gpio_set_pin_level(Camera_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(Camera_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Camera_SCK, PINMUX_PC13C_SERCOM7_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(Camera_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(Camera_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(Camera_MISO, PINMUX_PA30C_SERCOM7_PAD2);
}

void SPI_CAMERA_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM7_GCLK_ID_CORE, CONF_GCLK_SERCOM7_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM7_GCLK_ID_SLOW, CONF_GCLK_SERCOM7_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM7_bit(MCLK);
}

void SPI_CAMERA_init(void)
{
	SPI_CAMERA_CLOCK_init();
	spi_m_sync_init(&SPI_CAMERA, SERCOM7);
	SPI_CAMERA_PORT_init();
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

	// GPIO on PB01

	gpio_set_pin_level(LED_RED,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_RED, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_RED, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB12

	gpio_set_pin_level(Display_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Display_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Display_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB13

	gpio_set_pin_level(Display_DC,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Display_DC, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Display_DC, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB20

	gpio_set_pin_level(Display_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Display_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Display_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB21

	gpio_set_pin_level(Camera_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Camera_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Camera_CS, GPIO_PIN_FUNCTION_OFF);

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

	// GPIO on PC18

	gpio_set_pin_level(Photodiode_MUX_S0,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Photodiode_MUX_S0, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Photodiode_MUX_S0, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC19

	gpio_set_pin_level(Photodiode_MUX_S1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Photodiode_MUX_S1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Photodiode_MUX_S1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC20

	gpio_set_pin_level(Photodiode_MUX_S2,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Photodiode_MUX_S2, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Photodiode_MUX_S2, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC21

	gpio_set_pin_level(Photodiode_MUX_EN,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(Photodiode_MUX_EN, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(Photodiode_MUX_EN, GPIO_PIN_FUNCTION_OFF);

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

	// GPIO on PD21

	// Set pin direction to input
	gpio_set_pin_direction(Magnetometer_DRDY, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(Magnetometer_DRDY,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_DOWN);

	gpio_set_pin_function(Magnetometer_DRDY, GPIO_PIN_FUNCTION_OFF);

	ADC_0_init();

	FLASH_0_init();

	I2C_SBAND_init();

	I2C_MAG_GYRO_init();

	I2C_CAMERA_init();

	SPI_MRAM_init();

	SPI_DISPLAY_init();

	SPI_CAMERA_init();

	delay_driver_init();

	RAND_0_init();

	WDT_0_init();
}
