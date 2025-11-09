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

void I2C_SBAND_example(void)
{
	struct io_descriptor *I2C_SBAND_io;

	i2c_m_sync_get_io_descriptor(&I2C_SBAND, &I2C_SBAND_io);
	i2c_m_sync_enable(&I2C_SBAND);
	i2c_m_sync_set_slaveaddr(&I2C_SBAND, 0x12, I2C_M_SEVEN);
	io_write(I2C_SBAND_io, (uint8_t *)"Hello World!", 12);
}

void I2C_MAG_GYRO_example(void)
{
	struct io_descriptor *I2C_MAG_GYRO_io;

	i2c_m_sync_get_io_descriptor(&I2C_MAG_GYRO, &I2C_MAG_GYRO_io);
	i2c_m_sync_enable(&I2C_MAG_GYRO);
	i2c_m_sync_set_slaveaddr(&I2C_MAG_GYRO, 0x12, I2C_M_SEVEN);
	io_write(I2C_MAG_GYRO_io, (uint8_t *)"Hello World!", 12);
}

void I2C_CAMERA_example(void)
{
	struct io_descriptor *I2C_CAMERA_io;

	i2c_m_sync_get_io_descriptor(&I2C_CAMERA, &I2C_CAMERA_io);
	i2c_m_sync_enable(&I2C_CAMERA);
	i2c_m_sync_set_slaveaddr(&I2C_CAMERA, 0x12, I2C_M_SEVEN);
	io_write(I2C_CAMERA_io, (uint8_t *)"Hello World!", 12);
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
 * Example of using SPI_DISPLAY to write "Hello World" using the IO abstraction.
 */
static uint8_t example_SPI_DISPLAY[12] = "Hello World!";

void SPI_DISPLAY_example(void)
{
	struct io_descriptor *io;
	spi_m_sync_get_io_descriptor(&SPI_DISPLAY, &io);

	spi_m_sync_enable(&SPI_DISPLAY);
	io_write(io, example_SPI_DISPLAY, 12);
}

/**
 * Example of using SPI_CAMERA to write "Hello World" using the IO abstraction.
 */
static uint8_t example_SPI_CAMERA[12] = "Hello World!";

void SPI_CAMERA_example(void)
{
	struct io_descriptor *io;
	spi_m_sync_get_io_descriptor(&SPI_CAMERA, &io);

	spi_m_sync_enable(&SPI_CAMERA);
	io_write(io, example_SPI_CAMERA, 12);
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
