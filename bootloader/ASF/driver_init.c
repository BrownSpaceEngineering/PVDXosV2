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
struct spi_m_sync_descriptor SPI_MAGNETOMETER_GYRO;
struct spi_m_sync_descriptor SPI_SBAND;
struct spi_m_sync_descriptor SPI_UHF;

struct adc_sync_descriptor ADC_0;

struct adc_sync_descriptor ADC_1;

struct dac_sync_descriptor DAC_0;

struct i2c_m_sync_desc I2C_SBAND;

struct i2c_m_sync_desc I2C_CAMERA;

struct pwm_descriptor MAGNETORQUER_1;

struct pwm_descriptor MAGNETORQUER_2;

struct rand_sync_desc RAND_0;

struct wdt_descriptor WDT_0;

void ADC_0_PORT_init(void)
{

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_1, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_1, PINMUX_PA03B_ADC0_AIN1);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_2, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_2, PINMUX_PB08B_ADC0_AIN2);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_3, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_3, PINMUX_PB09B_ADC0_AIN3);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_4, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_4, PINMUX_PA04B_ADC0_AIN4);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_5, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_5, PINMUX_PA05B_ADC0_AIN5);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_6, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_6, PINMUX_PA06B_ADC0_AIN6);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_7, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_7, PINMUX_PA07B_ADC0_AIN7);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_8, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_8, PINMUX_PA08B_ADC0_AIN8);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_9, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_9, PINMUX_PA09B_ADC0_AIN9);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_10, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_10, PINMUX_PA10B_ADC0_AIN10);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_11, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_11, PINMUX_PA11B_ADC0_AIN11);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_12, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_12, PINMUX_PB00B_ADC0_AIN12);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_13, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_13, PINMUX_PB01B_ADC0_AIN13);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_14, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_14, PINMUX_PB02B_ADC0_AIN14);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_15, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_15, PINMUX_PB03B_ADC0_AIN15);
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
	gpio_set_pin_direction(PHD_16, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_16, PINMUX_PC02B_ADC1_AIN4);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_17, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_17, PINMUX_PC03B_ADC1_AIN5);

	// Disable digital pin circuitry
	gpio_set_pin_direction(THERM_0, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(THERM_0, PINMUX_PB04B_ADC1_AIN6);

	// Disable digital pin circuitry
	gpio_set_pin_direction(THERM_1, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(THERM_1, PINMUX_PB05B_ADC1_AIN7);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB06, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB06, PINMUX_PB06B_ADC1_AIN8);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PB07, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PB07, PINMUX_PB07B_ADC1_AIN9);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PHD_0, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PHD_0, PINMUX_PC00B_ADC1_AIN10);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC01, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC01, PINMUX_PC01B_ADC1_AIN11);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC30, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC30, PINMUX_PC30B_ADC1_AIN12);

	// Disable digital pin circuitry
	gpio_set_pin_direction(PC31, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PC31, PINMUX_PC31B_ADC1_AIN13);

	// Disable digital pin circuitry
	gpio_set_pin_direction(THERM_2, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(THERM_2, PINMUX_PD00B_ADC1_AIN14);

	// Disable digital pin circuitry
	gpio_set_pin_direction(THERM_3, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(THERM_3, PINMUX_PD01B_ADC1_AIN15);
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

void DAC_0_PORT_init(void)
{

	// Disable digital pin circuitry
	gpio_set_pin_direction(PVD_CRNT, GPIO_DIRECTION_OFF);

	gpio_set_pin_function(PVD_CRNT, PINMUX_PA02B_DAC_VOUT0);
}

void DAC_0_CLOCK_init(void)
{

	hri_mclk_set_APBDMASK_DAC_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, DAC_GCLK_ID, CONF_GCLK_DAC_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void DAC_0_init(void)
{
	DAC_0_CLOCK_init();
	dac_sync_init(&DAC_0, DAC);
	DAC_0_PORT_init();
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

	gpio_set_pin_function(MRAM_MOSI, PINMUX_PB24C_SERCOM0_PAD0);

	gpio_set_pin_level(MRAM_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM_SCK, PINMUX_PB25C_SERCOM0_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(MRAM_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(MRAM_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MRAM_MISO, PINMUX_PC18D_SERCOM0_PAD2);
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

	gpio_set_pin_function(DISPLAY_MOSI, PINMUX_PA00D_SERCOM1_PAD0);

	gpio_set_pin_level(DISPLAY_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_SCK, PINMUX_PA01D_SERCOM1_PAD1);

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

	gpio_set_pin_function(CAMERA_MOSI, PINMUX_PB26C_SERCOM2_PAD0);

	gpio_set_pin_level(CAMERA_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CAMERA_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CAMERA_SCK, PINMUX_PB27C_SERCOM2_PAD1);

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

void SPI_MAGNETOMETER_GYRO_PORT_init(void)
{

	gpio_set_pin_level(MAGNETOMETER_GYRO_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MAGNETOMETER_GYRO_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MAGNETOMETER_GYRO_MOSI, PINMUX_PA13D_SERCOM4_PAD0);

	gpio_set_pin_level(MAGNETOMETER_GYRO_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MAGNETOMETER_GYRO_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MAGNETOMETER_GYRO_SCK, PINMUX_PA12D_SERCOM4_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(MAGNETOMETER_GYRO_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(MAGNETOMETER_GYRO_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(MAGNETOMETER_GYRO_MISO, PINMUX_PB14C_SERCOM4_PAD2);
}

void SPI_MAGNETOMETER_GYRO_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_SLOW, CONF_GCLK_SERCOM4_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM4_bit(MCLK);
}

void SPI_MAGNETOMETER_GYRO_init(void)
{
	SPI_MAGNETOMETER_GYRO_CLOCK_init();
	spi_m_sync_init(&SPI_MAGNETOMETER_GYRO, SERCOM4);
	SPI_MAGNETOMETER_GYRO_PORT_init();
}

void SPI_SBAND_PORT_init(void)
{

	gpio_set_pin_level(SBAND_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(SBAND_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(SBAND_MOSI, PINMUX_PB16C_SERCOM5_PAD0);

	gpio_set_pin_level(SBAND_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(SBAND_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(SBAND_SCK, PINMUX_PB17C_SERCOM5_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(SBAND_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(SBAND_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(SBAND_MISO, PINMUX_PB18C_SERCOM5_PAD2);
}

void SPI_SBAND_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_CORE, CONF_GCLK_SERCOM5_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM5_GCLK_ID_SLOW, CONF_GCLK_SERCOM5_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM5_bit(MCLK);
}

void SPI_SBAND_init(void)
{
	SPI_SBAND_CLOCK_init();
	spi_m_sync_init(&SPI_SBAND, SERCOM5);
	SPI_SBAND_PORT_init();
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

	gpio_set_pin_function(CAMERA_SDA, PINMUX_PD09D_SERCOM6_PAD0);

	gpio_set_pin_pull_mode(CAMERA_SCL,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CAMERA_SCL, PINMUX_PD08D_SERCOM6_PAD1);
}

void I2C_CAMERA_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM6_GCLK_ID_CORE, CONF_GCLK_SERCOM6_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM6_GCLK_ID_SLOW, CONF_GCLK_SERCOM6_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM6_bit(MCLK);
}

void I2C_CAMERA_init(void)
{
	I2C_CAMERA_CLOCK_init();
	i2c_m_sync_init(&I2C_CAMERA, SERCOM6);
	I2C_CAMERA_PORT_init();
}

void SPI_UHF_PORT_init(void)
{

	gpio_set_pin_level(UHF_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_MOSI, PINMUX_PC12C_SERCOM7_PAD0);

	gpio_set_pin_level(UHF_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_SCK, PINMUX_PC13C_SERCOM7_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(UHF_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(UHF_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(UHF_MISO, PINMUX_PD10C_SERCOM7_PAD2);
}

void SPI_UHF_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM7_GCLK_ID_CORE, CONF_GCLK_SERCOM7_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM7_GCLK_ID_SLOW, CONF_GCLK_SERCOM7_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM7_bit(MCLK);
}

void SPI_UHF_init(void)
{
	SPI_UHF_CLOCK_init();
	spi_m_sync_init(&SPI_UHF, SERCOM7);
	SPI_UHF_PORT_init();
}

void delay_driver_init(void)
{
	delay_init(SysTick);
}

void MAGNETORQUER_1_PORT_init(void)
{

	gpio_set_pin_function(PA20, PINMUX_PA20G_TCC0_WO0);

	gpio_set_pin_function(PA21, PINMUX_PA21G_TCC0_WO1);

	gpio_set_pin_function(PA22, PINMUX_PA22G_TCC0_WO2);

	gpio_set_pin_function(PA23, PINMUX_PA23G_TCC0_WO3);
}

void MAGNETORQUER_1_CLOCK_init(void)
{

	hri_mclk_set_APBBMASK_TCC0_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, TCC0_GCLK_ID, CONF_GCLK_TCC0_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void MAGNETORQUER_1_init(void)
{
	MAGNETORQUER_1_CLOCK_init();
	MAGNETORQUER_1_PORT_init();
	pwm_init(&MAGNETORQUER_1, TCC0, _tcc_get_pwm());
}

void MAGNETORQUER_2_PORT_init(void)
{

	gpio_set_pin_function(PD20, PINMUX_PD20F_TCC1_WO0);

	gpio_set_pin_function(PD21, PINMUX_PD21F_TCC1_WO1);
}

void MAGNETORQUER_2_CLOCK_init(void)
{

	hri_mclk_set_APBBMASK_TCC1_bit(MCLK);
	hri_gclk_write_PCHCTRL_reg(GCLK, TCC1_GCLK_ID, CONF_GCLK_TCC1_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
}

void MAGNETORQUER_2_init(void)
{
	MAGNETORQUER_2_CLOCK_init();
	MAGNETORQUER_2_PORT_init();
	pwm_init(&MAGNETORQUER_2, TCC1, _tcc_get_pwm());
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

	// GPIO on PA15

	gpio_set_pin_level(LED_ORANGE2,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_ORANGE2, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_ORANGE2, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PA19

	gpio_set_pin_level(LED_RED,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_RED, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_RED, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB10

	gpio_set_pin_level(MRAM3_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB11

	gpio_set_pin_level(MRAM3_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB12

	gpio_set_pin_level(MRAM1_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM1_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM1_WP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB13

	gpio_set_pin_level(MRAM2_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM2_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM2_WP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB15

	gpio_set_pin_level(MRAM3_WP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MRAM3_WP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MRAM3_WP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB19

	gpio_set_pin_level(BURN,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(BURN, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(BURN, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB20

	gpio_set_pin_level(EPS_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(EPS_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(EPS_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB21

	gpio_set_pin_level(EPS_A0,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(EPS_A0, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(EPS_A0, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB22

	gpio_set_pin_level(EPS_A1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(EPS_A1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(EPS_A1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB23

	gpio_set_pin_level(EPS_A3,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(EPS_A3, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(EPS_A3, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB28

	// Set pin direction to input
	gpio_set_pin_direction(EPS_IRQ, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(EPS_IRQ,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(EPS_IRQ, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB30

	gpio_set_pin_level(MTQ_X_SLP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MTQ_X_SLP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MTQ_X_SLP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB31

	gpio_set_pin_level(MTQ_Y_SLP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MTQ_Y_SLP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MTQ_Y_SLP, GPIO_PIN_FUNCTION_OFF);

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

	// GPIO on PC11

	gpio_set_pin_level(DISPLAY_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC14

	gpio_set_pin_level(CAMERA_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CAMERA_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC15

	gpio_set_pin_level(LED_ORANGE1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(LED_ORANGE1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(LED_ORANGE1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC16

	gpio_set_pin_level(UHF_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC17

	gpio_set_pin_level(UHF_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(UHF_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(UHF_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC19

	gpio_set_pin_level(MAGNETOMETER_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(MAGNETOMETER_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MAGNETOMETER_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC20

	gpio_set_pin_level(GYRO_CS,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(GYRO_CS, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(GYRO_CS, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC23

	gpio_set_pin_level(HEATER_CTRL_1,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(HEATER_CTRL_1, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(HEATER_CTRL_1, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC24

	gpio_set_pin_level(HEATER_CTRL_2,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(HEATER_CTRL_2, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(HEATER_CTRL_2, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PC27

	gpio_set_pin_level(MTQ_Z_SLP,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(MTQ_Z_SLP, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(MTQ_Z_SLP, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PD11

	gpio_set_pin_level(DISPLAY_RST,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_RST, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_RST, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PD12

	gpio_set_pin_level(DISPLAY_DC,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(DISPLAY_DC, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(DISPLAY_DC, GPIO_PIN_FUNCTION_OFF);

	ADC_0_init();

	ADC_1_init();

	DAC_0_init();

	TIMER_0_init();

	SPI_MRAM_init();

	SPI_DISPLAY_init();

	SPI_CAMERA_init();

	I2C_SBAND_init();

	SPI_MAGNETOMETER_GYRO_init();

	SPI_SBAND_init();

	I2C_CAMERA_init();

	SPI_UHF_init();

	delay_driver_init();

	MAGNETORQUER_1_init();

	MAGNETORQUER_2_init();

	RAND_0_init();

	WDT_0_init();
}
