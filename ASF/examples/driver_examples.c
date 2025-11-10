/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */

#include "driver_examples.h"
#include "driver_init.h"
#include "utils.h"

/**
 * Example of using ADC_0 to generate waveform.
 */
void ADC_0_example(void)
{
	uint8_t buffer[2];

	adc_sync_enable_channel(&ADC_0, 0);

	while (1) {
		adc_sync_read_channel(&ADC_0, 0, buffer, 2);
	}
}

/**
 * Example of using ADC_1 to generate waveform.
 */
void ADC_1_example(void)
{
	uint8_t buffer[2];

	adc_sync_enable_channel(&ADC_1, 0);

	while (1) {
		adc_sync_read_channel(&ADC_1, 0, buffer, 2);
	}
}

static struct timer_task TIMER_0_task1, TIMER_0_task2;
/**
 * Example of using TIMER_0.
 */
static void TIMER_0_task1_cb(const struct timer_task *const timer_task)
{
}

static void TIMER_0_task2_cb(const struct timer_task *const timer_task)
{
}

void TIMER_0_example(void)
{
	TIMER_0_task1.interval = 100;
	TIMER_0_task1.cb       = TIMER_0_task1_cb;
	TIMER_0_task1.mode     = TIMER_TASK_REPEAT;
	TIMER_0_task2.interval = 200;
	TIMER_0_task2.cb       = TIMER_0_task2_cb;
	TIMER_0_task2.mode     = TIMER_TASK_REPEAT;

	timer_add_task(&TIMER_0, &TIMER_0_task1);
	timer_add_task(&TIMER_0, &TIMER_0_task2);
	timer_start(&TIMER_0);
}

/**
 * Example of using SPI_MRAM to write "Hello World" using the IO abstraction.
 */
static uint8_t example_SPI_MRAM[12] = "Hello World!";

void SPI_MRAM_example(void)
{
	struct io_descriptor *io;
	spi_m_sync_get_io_descriptor(&SPI_MRAM, &io);

	spi_m_sync_enable(&SPI_MRAM);
	io_write(io, example_SPI_MRAM, 12);
}

/**
 * Example of using SPI_Display to write "Hello World" using the IO abstraction.
 */
static uint8_t example_SPI_Display[12] = "Hello World!";

void SPI_Display_example(void)
{
	struct io_descriptor *io;
	spi_m_sync_get_io_descriptor(&SPI_Display, &io);

	spi_m_sync_enable(&SPI_Display);
	io_write(io, example_SPI_Display, 12);
}

/**
 * Example of using SPI_Camera to write "Hello World" using the IO abstraction.
 */
static uint8_t example_SPI_Camera[12] = "Hello World!";

void SPI_Camera_example(void)
{
	struct io_descriptor *io;
	spi_m_sync_get_io_descriptor(&SPI_Camera, &io);

	spi_m_sync_enable(&SPI_Camera);
	io_write(io, example_SPI_Camera, 12);
}

void I2C_SBand_example(void)
{
	struct io_descriptor *I2C_SBand_io;

	i2c_m_sync_get_io_descriptor(&I2C_SBand, &I2C_SBand_io);
	i2c_m_sync_enable(&I2C_SBand);
	i2c_m_sync_set_slaveaddr(&I2C_SBand, 0x12, I2C_M_SEVEN);
	io_write(I2C_SBand_io, (uint8_t *)"Hello World!", 12);
}

void I2C_Mag_Gyro_example(void)
{
	struct io_descriptor *I2C_Mag_Gyro_io;

	i2c_m_sync_get_io_descriptor(&I2C_Mag_Gyro, &I2C_Mag_Gyro_io);
	i2c_m_sync_enable(&I2C_Mag_Gyro);
	i2c_m_sync_set_slaveaddr(&I2C_Mag_Gyro, 0x12, I2C_M_SEVEN);
	io_write(I2C_Mag_Gyro_io, (uint8_t *)"Hello World!", 12);
}

void I2C_Camera_example(void)
{
	struct io_descriptor *I2C_Camera_io;

	i2c_m_sync_get_io_descriptor(&I2C_Camera, &I2C_Camera_io);
	i2c_m_sync_enable(&I2C_Camera);
	i2c_m_sync_set_slaveaddr(&I2C_Camera, 0x12, I2C_M_SEVEN);
	io_write(I2C_Camera_io, (uint8_t *)"Hello World!", 12);
}

void delay_example(void)
{
	delay_ms(5000);
}

/**
 * Example of using RAND_0 to generate waveform.
 */
void RAND_0_example(void)
{
	uint32_t random_n[4];
	rand_sync_enable(&RAND_0);
	random_n[0] = rand_sync_read32(&RAND_0);
	random_n[1] = rand_sync_read32(&RAND_0);
	rand_sync_read_buf32(&RAND_0, &random_n[2], 2);
	if (random_n[0] == random_n[1]) {
		/* halt */
		while (1)
			;
	}
	if (random_n[2] == random_n[3]) {
		/* halt */
		while (1)
			;
	}
}

/**
 * Example of using WDT_0.
 */
void WDT_0_example(void)
{
	uint32_t clk_rate;
	uint16_t timeout_period;

	clk_rate       = 1000;
	timeout_period = 4096;
	wdt_set_timeout_period(&WDT_0, clk_rate, timeout_period);
	wdt_enable(&WDT_0);
}
