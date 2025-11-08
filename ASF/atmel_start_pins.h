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

#define SBAND_SCL GPIO(GPIO_PORTA, 8)
#define SBAND_SDA GPIO(GPIO_PORTA, 9)
#define Camera_SCL GPIO(GPIO_PORTA, 12)
#define Camera_SDA GPIO(GPIO_PORTA, 13)
#define Magnetometer_Gyro_SCL GPIO(GPIO_PORTA, 16)
#define Magnetometer_Gyro_SDA GPIO(GPIO_PORTA, 17)
#define Camera_MISO GPIO(GPIO_PORTA, 30)
#define LED_RED GPIO(GPIO_PORTB, 1)
#define PB08 GPIO(GPIO_PORTB, 8)
#define Display_RST GPIO(GPIO_PORTB, 12)
#define Display_DC GPIO(GPIO_PORTB, 13)
#define MRAM_MOSI GPIO(GPIO_PORTB, 16)
#define MRAM_SCK GPIO(GPIO_PORTB, 17)
#define MRAM_MISO GPIO(GPIO_PORTB, 18)
#define Display_CS GPIO(GPIO_PORTB, 20)
#define Camera_CS GPIO(GPIO_PORTB, 21)
#define Camera_MOSI GPIO(GPIO_PORTB, 30)
#define MRAM1_CS GPIO(GPIO_PORTC, 4)
#define MRAM1_RST GPIO(GPIO_PORTC, 5)
#define MRAM2_CS GPIO(GPIO_PORTC, 6)
#define MRAM2_RST GPIO(GPIO_PORTC, 7)
#define MRAM3_RST GPIO(GPIO_PORTC, 10)
#define MRAM3_CS GPIO(GPIO_PORTC, 11)
#define MRAM3_WP GPIO(GPIO_PORTC, 12)
#define Camera_SCK GPIO(GPIO_PORTC, 13)
#define MRAM1_WP GPIO(GPIO_PORTC, 14)
#define MRAM2_WP GPIO(GPIO_PORTC, 15)
#define Photodiode_MUX_S0 GPIO(GPIO_PORTC, 18)
#define Photodiode_MUX_S1 GPIO(GPIO_PORTC, 19)
#define Photodiode_MUX_S2 GPIO(GPIO_PORTC, 20)
#define Photodiode_MUX_EN GPIO(GPIO_PORTC, 21)
#define LED_Orange1 GPIO(GPIO_PORTC, 30)
#define LED_Orange2 GPIO(GPIO_PORTC, 31)
#define Display_SCK GPIO(GPIO_PORTD, 8)
#define Display_MOSI GPIO(GPIO_PORTD, 9)
#define Display_MISO GPIO(GPIO_PORTD, 10)
#define Magnetometer_DRDY GPIO(GPIO_PORTD, 21)

#endif // ATMEL_START_PINS_H_INCLUDED
