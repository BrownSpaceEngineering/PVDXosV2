/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */
#ifndef DRIVER_INIT_INCLUDED
#define DRIVER_INIT_INCLUDED

#include "atmel_start_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <hal_atomic.h>
#include <hal_delay.h>
#include <hal_gpio.h>
#include <hal_init.h>
#include <hal_io.h>
#include <hal_sleep.h>

#include <hal_adc_sync.h>

#include <hal_flash.h>

#include <hal_i2c_m_sync.h>

#include <hal_i2c_m_sync.h>

#include <hal_i2c_m_sync.h>
#include <hal_spi_m_sync.h>
#include <hal_spi_m_sync.h>
#include <hal_spi_m_sync.h>

#include <hal_delay.h>

#include <hal_rand_sync.h>

#include <hal_wdt.h>

extern struct adc_sync_descriptor ADC_0;

extern struct flash_descriptor FLASH_0;

extern struct i2c_m_sync_desc I2C_SBAND;

extern struct i2c_m_sync_desc I2C_MAG_GYRO;

extern struct i2c_m_sync_desc       I2C_CAMERA;
extern struct spi_m_sync_descriptor SPI_MRAM;
extern struct spi_m_sync_descriptor SPI_DISPLAY;
extern struct spi_m_sync_descriptor SPI_CAMERA;

extern struct rand_sync_desc RAND_0;

extern struct wdt_descriptor WDT_0;

void ADC_0_PORT_init(void);
void ADC_0_CLOCK_init(void);
void ADC_0_init(void);

void FLASH_0_init(void);
void FLASH_0_CLOCK_init(void);

void I2C_SBAND_CLOCK_init(void);
void I2C_SBAND_init(void);
void I2C_SBAND_PORT_init(void);

void I2C_MAG_GYRO_CLOCK_init(void);
void I2C_MAG_GYRO_init(void);
void I2C_MAG_GYRO_PORT_init(void);

void I2C_CAMERA_CLOCK_init(void);
void I2C_CAMERA_init(void);
void I2C_CAMERA_PORT_init(void);

void SPI_MRAM_PORT_init(void);
void SPI_MRAM_CLOCK_init(void);
void SPI_MRAM_init(void);

void SPI_DISPLAY_PORT_init(void);
void SPI_DISPLAY_CLOCK_init(void);
void SPI_DISPLAY_init(void);

void SPI_CAMERA_PORT_init(void);
void SPI_CAMERA_CLOCK_init(void);
void SPI_CAMERA_init(void);

void delay_driver_init(void);

void RAND_0_CLOCK_init(void);
void RAND_0_init(void);

void WDT_0_CLOCK_init(void);
void WDT_0_init(void);

/**
 * \brief Perform system initialization, initialize pins and clocks for
 * peripherals
 */
void system_init(void);

#ifdef __cplusplus
}
#endif
#endif // DRIVER_INIT_INCLUDED
