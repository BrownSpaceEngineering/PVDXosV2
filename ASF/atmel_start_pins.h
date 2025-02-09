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

#define Camera_SCK GPIO(GPIO_PORTA, 8)
#define Camera_MOSI GPIO(GPIO_PORTA, 9)
#define Camera_MISO GPIO(GPIO_PORTA, 10)
#define Shared_SDA GPIO(GPIO_PORTA, 16)
#define Shared_SCL GPIO(GPIO_PORTA, 17)
#define Display_MISO_unused GPIO(GPIO_PORTA, 18)
#define LED_Red GPIO(GPIO_PORTB, 1)
#define Display_RST GPIO(GPIO_PORTB, 12)
#define Display_DC GPIO(GPIO_PORTB, 13)
#define Display_CS GPIO(GPIO_PORTB, 20)
#define Display_SCK GPIO(GPIO_PORTC, 22)
#define Display_MOSI GPIO(GPIO_PORTC, 23)
#define LED_Orange1 GPIO(GPIO_PORTC, 30)
#define LED_Orange2 GPIO(GPIO_PORTC, 31)
#define Magnetometer_DRDY GPIO(GPIO_PORTD, 21)

#endif // ATMEL_START_PINS_H_INCLUDED
