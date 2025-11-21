/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */
#ifndef ATMEL_START_PINS_H_INCLUDED
#define ATMEL_START_PINS_H_INCLUDED

#include <hal_gpio.h>

// SAMD51 has 14 pin functions

#define GPIO_PIN_FUNCTION_A 0
#define GPIO_PIN_FUNCTION_B 1
#define GPIO_PIN_FUNCTION_C 2
#define GPIO_PIN_FUNCTION_D 3
#define GPIO_PIN_FUNCTION_E 4
#define GPIO_PIN_FUNCTION_F 5
#define GPIO_PIN_FUNCTION_G 6
#define GPIO_PIN_FUNCTION_H 7
#define GPIO_PIN_FUNCTION_I 8
#define GPIO_PIN_FUNCTION_J 9
#define GPIO_PIN_FUNCTION_K 10
#define GPIO_PIN_FUNCTION_L 11
#define GPIO_PIN_FUNCTION_M 12
#define GPIO_PIN_FUNCTION_N 13

#define MRAM_MOSI GPIO(GPIO_PORTB, 16)
#define MRAM_SCK GPIO(GPIO_PORTB, 17)
#define MRAM_MISO GPIO(GPIO_PORTB, 18)
#define MRAM1_CS GPIO(GPIO_PORTC, 4)
#define MRAM1_RST GPIO(GPIO_PORTC, 5)
#define MRAM2_CS GPIO(GPIO_PORTC, 6)
#define MRAM2_RST GPIO(GPIO_PORTC, 7)
#define MRAM3_RST GPIO(GPIO_PORTC, 10)
#define MRAM3_CS GPIO(GPIO_PORTC, 11)
#define MRAM3_WP GPIO(GPIO_PORTC, 12)
#define MRAM1_WP GPIO(GPIO_PORTC, 14)
#define MRAM2_WP GPIO(GPIO_PORTC, 15)

#endif // ATMEL_START_PINS_H_INCLUDED
