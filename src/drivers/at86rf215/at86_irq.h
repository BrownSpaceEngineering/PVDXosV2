#ifndef AT86_IRQ_H
#define AT86_IRQ_H

#include "at86_pvdx.h"
#include "globals.h"

// Interrupt line of the AT86RF215's IRQ pin (UHF_IRQ, PA27)
#define AT86_IRQ_EXTINT 11

void at86_irq_init(void);
bool at86_irq_wait(struct at86rf215 *h, at86_irqs_t *irqs, uint32_t timeout_ms);

#endif // AT86_IRQ_H
