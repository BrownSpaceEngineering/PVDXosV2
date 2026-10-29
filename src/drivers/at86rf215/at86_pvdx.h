/**
* Extracted helpers from `radio-task` for UHF functionality
* Taken by Alex on 10/29. should rip these out before merge.
*/

#ifndef AT86_PVDX_H
#define AT86_PVDX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at86rf215.h"

#define AT86_RF_IRQ_WAKEUP (1u << 0)
#define AT86_RF_IRQ_TRXRDY (1u << 1)
#define AT86_RF_IRQ_TRXERR (1u << 4)
#define AT86_BBC_IRQ_RXFS  (1u << 0)
#define AT86_BBC_IRQ_RXFE  (1u << 1)
#define AT86_BBC_IRQ_TXFE  (1u << 4)

#define AT86_BBC_PC_FCSOK   (1u << 5)
#define AT86_AUXS_EXTLNABYP (1u << 7)
#define AT86_FCS_LEN        2

#define AT86_PADFE_TX (AT86RF215_RF_FEMODE1 << 6)
#define AT86_PADFE_RX (AT86RF215_RF_FEMODE0 << 6)

typedef enum
{
  AT86_O_QPSK_LEG              = 0,
  AT86_MR_O_QPSK_100CH_6_2_5KB = 1,
  AT86_MR_O_QPSK_100CH_12_5KB  = 2,
  AT86_MR_FSK                  = 3,
  AT86_MR_FSK_LINK             = 4,
} at86_config_t;

typedef struct
{
  uint8_t part_number;
  uint8_t version;
  uint8_t rf09_state;
} at86_health_t;

typedef struct
{
  uint8_t rf;
  uint8_t bbc;
} at86_irqs_t;

int
at86_init_pvdx(struct at86rf215 *radio);
int
change_at86_config(struct at86rf215 *h, at86_config_t config);
int
at86_check_health(struct at86rf215 *h, at86_health_t *health);

at86_irqs_t
at86_read_irqs(struct at86rf215 *h);
int
at86_go_txprep(struct at86rf215 *h);
int
at86_go_rx(struct at86rf215 *h);
int
at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz);

#endif // AT86_PVDX_H
