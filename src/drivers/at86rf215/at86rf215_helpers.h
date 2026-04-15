#ifndef AT86RF215_HELPERS_H
#define AT86RF215_HELPERS_H

#include <atmel_start.h>

//   UHF_MISO: PD10
//   UHF_MOSI: PC12
//   UHF_SCK:  PC13
//   UHF_CS: PA16
//   UHF_RST: PC17

#define UHF_CS_LOW()  gpio_set_pin_level(UHF_CS, 0)
#define UHF_CS_HIGH() gpio_set_pin_level(UHF_CS, 1)

#define UHF_RST_LOW()  gpio_set_pin_level(UHF_RST, 0)
#define UHF_RST_HIGH() gpio_set_pin_level(UHF_RST, 1)

#define UHF_SPI_BUF_SIZE (AT86RF215_MAX_PDU + 2)

#endif // AT86RF215_HELPERS_H
