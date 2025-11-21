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
