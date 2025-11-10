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

#define PA02 GPIO(GPIO_PORTA, 2)
#define PA03 GPIO(GPIO_PORTA, 3)
#define MRAM_MOSI GPIO(GPIO_PORTA, 4)
#define MRAM_SCK GPIO(GPIO_PORTA, 5)
#define MRAM_MISO GPIO(GPIO_PORTA, 6)
#define PA07 GPIO(GPIO_PORTA, 7)
#define PA11 GPIO(GPIO_PORTA, 11)
#define Magnetometer_Gyro_SCL GPIO(GPIO_PORTA, 12)
#define Magnetometer_Gyro_SDA GPIO(GPIO_PORTA, 13)
#define Camera_MISO GPIO(GPIO_PORTA, 14)
#define SBand_SCL GPIO(GPIO_PORTA, 16)
#define SBand_SDA GPIO(GPIO_PORTA, 17)
#define Display_MISO GPIO(GPIO_PORTA, 18)
#define Camera_SCL GPIO(GPIO_PORTA, 22)
#define Camera_SDA GPIO(GPIO_PORTA, 23)
#define PB00 GPIO(GPIO_PORTB, 0)
#define PB01 GPIO(GPIO_PORTB, 1)
#define PB02 GPIO(GPIO_PORTB, 2)
#define PB03 GPIO(GPIO_PORTB, 3)
#define PB04 GPIO(GPIO_PORTB, 4)
#define PB05 GPIO(GPIO_PORTB, 5)
#define PB06 GPIO(GPIO_PORTB, 6)
#define PB07 GPIO(GPIO_PORTB, 7)
#define PB08 GPIO(GPIO_PORTB, 8)
#define PB09 GPIO(GPIO_PORTB, 9)
#define Magnetometer_DRDY GPIO(GPIO_PORTB, 12)
#define Display_CS GPIO(GPIO_PORTB, 13)
#define Display_RST GPIO(GPIO_PORTB, 14)
#define Display_DC GPIO(GPIO_PORTB, 15)
#define Camera_SCK GPIO(GPIO_PORTB, 24)
#define Camera_MOSI GPIO(GPIO_PORTB, 25)
#define PC00 GPIO(GPIO_PORTC, 0)
#define PC01 GPIO(GPIO_PORTC, 1)
#define PC02 GPIO(GPIO_PORTC, 2)
#define PC03 GPIO(GPIO_PORTC, 3)
#define MRAM1_CS GPIO(GPIO_PORTC, 4)
#define MRAM2_CS GPIO(GPIO_PORTC, 5)
#define MRAM3_CS GPIO(GPIO_PORTC, 6)
#define LED_Red GPIO(GPIO_PORTC, 7)
#define Display_MOSI GPIO(GPIO_PORTC, 22)
#define Display_SCK GPIO(GPIO_PORTC, 23)
#define LED_Orange1 GPIO(GPIO_PORTC, 30)
#define LED_Orange2 GPIO(GPIO_PORTC, 31)
#define MRAM1_RST GPIO(GPIO_PORTD, 8)
#define MRAM2_RST GPIO(GPIO_PORTD, 9)
#define MRAM3_RST GPIO(GPIO_PORTD, 10)

#endif // ATMEL_START_PINS_H_INCLUDED
