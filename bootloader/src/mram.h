#ifndef MRAM_H
#define MRAM_H

#include "atmel_start.h"
#include "hal_gpio.h"
#include "hal_delay.h"

void mram_init(void);
void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);
void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);

#endif