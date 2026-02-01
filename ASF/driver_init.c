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
#include <hpl_adc_base.h>
#include <hpl_rtc_base.h>

struct timer_descriptor      TIMER_0;
struct spi_m_sync_descriptor SPI_MRAM;
struct spi_m_sync_descriptor SPI_DISPLAY;
struct spi_m_sync_descriptor SPI_CAMERA;

struct adc_sync_descriptor ADC_0;

struct adc_sync_descriptor ADC_1;

struct i2c_m_sync_desc I2C_SBAND;

struct i2c_m_sync_desc I2C_MAGNETOMETER_GYRO;

struct i2c_m_sync_desc I2C_CAMERA;

struct rand_sync_desc RAND_0;

struct wdt_descriptor WDT_0;

void ADC_0_PORT_init(void)
{

	// Disable digital pin circuitry
	gpio_set_pin_direction(PA02, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PA02, PINMUX_PA02B_ADC0_AIN0);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PA03, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PA03, PINMUX_PA03B_ADC0_AIN1);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB08, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB08, PINMUX_PB08B_ADC0_AIN2);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB09, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB09, PINMUX_PB09B_ADC0_AIN3);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PA07, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PA07, PINMUX_PA07B_ADC0_AIN7);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PA11, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PA11, PINMUX_PA11B_ADC0_AIN11);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB00, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB00, PINMUX_PB00B_ADC0_AIN12);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB01, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB01, PINMUX_PB01B_ADC0_AIN13);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB02, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB02, PINMUX_PB02B_ADC0_AIN14);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB03, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB03, PINMUX_PB03B_ADC0_AIN15);
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

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC02, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC02, PINMUX_PC02B_ADC1_AIN4);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC03, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC03, PINMUX_PC03B_ADC1_AIN5);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB04, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB04, PINMUX_PB04B_ADC1_AIN6);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB05, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB05, PINMUX_PB05B_ADC1_AIN7);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB06, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB06, PINMUX_PB06B_ADC1_AIN8);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB07, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB07, PINMUX_PB07B_ADC1_AIN9);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC00, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC00, PINMUX_PC00B_ADC1_AIN10);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC01, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC01, PINMUX_PC01B_ADC1_AIN11);
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

/**
 * \brief Timer initialization function
 *
 * Enables Timer peripheral, clocks and initializes Timer driver
 */
static void TIMER_0_init(void)
{
	hri_mclk_set_APBAMASK_RTC_bit(MCLK);
	timer_init(&TIMER_0, RTC, _rtc_get_timer());
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

	gpio_set_pin_function(MRAM_MOSI, PINMUX_PA04D_SERCOM0_PAD0);

	gpio_set_pin_level(MRAM_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM_SCK, PINMUX_PA05D_SERCOM0_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(MRAM_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(MRAM_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MRAM_MISO, PINMUX_PA06D_SERCOM0_PAD2);
}

void SPI_MRAM_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM0_GCLK_ID_CORE, CONF_GCLK_SERCOM0_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM0_GCLK_ID_SLOW, CONF_GCLK_SERCOM0_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBAMASK_SERCOM0_bit(MCLK);
}

void SPI_MRAM_init(void)
{
	SPI_MRAM_CLOCK_init();
	spi_m_sync_init(&SPI_MRAM, SERCOM0);
	SPI_MRAM_PORT_init();
}

void SPI_DISPLAY_PORT_init(void)
{

	gpio_set_pin_level(DISPLAY_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_MOSI, PINMUX_PC22C_SERCOM1_PAD0);

	gpio_set_pin_level(DISPLAY_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_SCK, PINMUX_PC23C_SERCOM1_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(DISPLAY_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(DISPLAY_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(DISPLAY_MISO, PINMUX_PA18C_SERCOM1_PAD2);
}

void SPI_DISPLAY_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM1_GCLK_ID_CORE, CONF_GCLK_SERCOM1_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM1_GCLK_ID_SLOW, CONF_GCLK_SERCOM1_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBAMASK_SERCOM1_bit(MCLK);
}

void SPI_DISPLAY_init(void)
{
	SPI_DISPLAY_CLOCK_init();
	spi_m_sync_init(&SPI_DISPLAY, SERCOM1);
	SPI_DISPLAY_PORT_init();
}

void SPI_CAMERA_PORT_init(void)
{

	gpio_set_pin_level(CAMERA_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CAMERA_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CAMERA_MOSI, PINMUX_PB25D_SERCOM2_PAD0);

	gpio_set_pin_level(CAMERA_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CAMERA_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CAMERA_SCK, PINMUX_PB24D_SERCOM2_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(CAMERA_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(CAMERA_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CAMERA_MISO, PINMUX_PA14C_SERCOM2_PAD2);
}

void SPI_CAMERA_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_CORE, CONF_GCLK_SERCOM2_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_SLOW, CONF_GCLK_SERCOM2_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM2_bit(MCLK);
}

void SPI_CAMERA_init(void)
{
	SPI_CAMERA_CLOCK_init();
	spi_m_sync_init(&SPI_CAMERA, SERCOM2);
	SPI_CAMERA_PORT_init();
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

	gpio_set_pin_function(SBAND_SDA, PINMUX_PA17D_SERCOM3_PAD0);

	gpio_set_pin_pull_mode(SBAND_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(SBAND_SCL, PINMUX_PA16D_SERCOM3_PAD1);
}

void I2C_SBAND_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_CORE, CONF_GCLK_SERCOM3_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM3_GCLK_ID_SLOW, CONF_GCLK_SERCOM3_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM3_bit(MCLK);
}

void I2C_SBAND_init(void)
{
	I2C_SBAND_CLOCK_init();
	i2c_m_sync_init(&I2C_SBAND, SERCOM3);
	I2C_SBAND_PORT_init();
}

void I2C_MAGNETOMETER_GYRO_PORT_init(void)
{

	gpio_set_pin_pull_mode(MAGNETOMETER_GYRO_SDA,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MAGNETOMETER_GYRO_SDA, PINMUX_PA13D_SERCOM4_PAD0);

	gpio_set_pin_pull_mode(MAGNETOMETER_GYRO_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MAGNETOMETER_GYRO_SCL, PINMUX_PA12D_SERCOM4_PAD1);
}

void I2C_MAGNETOMETER_GYRO_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_SLOW, CONF_GCLK_SERCOM4_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM4_bit(MCLK);
}

void I2C_MAGNETOMETER_GYRO_init(void)
{
	I2C_MAGNETOMETER_GYRO_CLOCK_init();
	i2c_m_sync_init(&I2C_MAGNETOMETER_GYRO, SERCOM4);
	I2C_MAGNETOMETER_GYRO_PORT_init();
}

void I2C_CAMERA_PORT_init(void)
{

	gpio_set_pin_pull_mode(CAMERA_SDA,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CAMERA_SDA, PINMUX_PA23D_SERCOM5_PAD0);

	gpio_set_pin_pull_mode(CAMERA_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CAMERA_SCL, PINMUX_PA22D_SERCOM5_PAD1);
}

void I2C_CAMERA_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_CORE, CONF_GCLK_SERCOM5_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_SLOW, CONF_GCLK_SERCOM5_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM5_bit(MCLK);
}

void I2C_CAMERA_init(void)
{
	I2C_CAMERA_CLOCK_init();
	i2c_m_sync_init(&I2C_CAMERA, SERCOM5);
	I2C_CAMERA_PORT_init();
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

	// GPIO on PB12

	// Set pin direction to input
	gpio_set_pin_direction(MAGNETOMETER_DRDY, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(MAGNETOMETER_DRDY,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_DOWN);

	gpio_set_pin_function(MAGNETOMETER_DRDY, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB13

	gpio_set_pin_level(DISPLAY_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB14

	gpio_set_pin_level(DISPLAY_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB15

	gpio_set_pin_level(DISPLAY_DC,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_DC, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_DC, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB19

	gpio_set_pin_level(CAMERA_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CAMERA_CS, GPIO_PIN_FUNCTION_OFF);

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

	gpio_set_pin_level(MRAM2_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC06

	gpio_set_pin_level(MRAM3_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC07

	gpio_set_pin_level(LED_RED,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_RED, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_RED, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC30

	gpio_set_pin_level(LED_ORANGE1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_ORANGE1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_ORANGE1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC31

	gpio_set_pin_level(LED_ORANGE2,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_ORANGE2, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_ORANGE2, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PD08

	gpio_set_pin_level(MRAM1_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM1_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM1_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PD09

	gpio_set_pin_level(MRAM2_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PD10

	gpio_set_pin_level(MRAM3_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_RST, GPIO_PIN_FUNCTION_OFF);

	ADC_0_init();

	ADC_1_init();

	TIMER_0_init();

	SPI_MRAM_init();

	SPI_DISPLAY_init();

	SPI_CAMERA_init();

	I2C_SBAND_init();

	I2C_MAGNETOMETER_GYRO_init();

	I2C_CAMERA_init();

	delay_driver_init();

	RAND_0_init();

	WDT_0_init();
}
