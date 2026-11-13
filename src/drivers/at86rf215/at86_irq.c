/**
 * at86_irq.c
 *
 * The AT86RF215's IRQ pin on EIC line AT86_IRQ_EXTINT. The radio holds the pin high while any unmasked interrupt reason
 * is pending in its IRQS registers, and reading those registers over SPI clears them. The ISR can't read them itself,
 * since a task may be in the middle of an SPI transfer to the radio, so it masks the line and wakes the task waiting in
 * at86_irq_wait(), which reads the registers. The line is only armed while a task waits on it.
 *
 * Created: November 11, 2026
 * Authors: PVDX
 */

#include "at86_irq.h"

#include "atmel_start.h"
#include "logging.h"
#include "regs.h"
#include "semphr.h"

#define AT86_IRQ_MASK (1u << AT86_IRQ_EXTINT)

static SemaphoreHandle_t irq_semaphore;
static StaticSemaphore_t irq_semaphore_buffer;

/**
 * \fn at86_irq_init
 *
 * \brief Routes UHF_IRQ to the EIC with high-level sensing, leaving the line disarmed. Can run before the scheduler.
 */
void at86_irq_init(void) {
    irq_semaphore = xSemaphoreCreateBinaryStatic(&irq_semaphore_buffer);
    if (irq_semaphore == NULL) {
        fatal("at86_irq: failed to create the IRQ semaphore\n");
    }

    hri_mclk_set_APBAMASK_EIC_bit(MCLK);
    // CONFIG and CKSEL can only be written while the EIC is disabled. CLK_ULP32K clocks the EIC, so it needs no GCLK.
    hri_eic_clear_CTRLA_ENABLE_bit(EIC);
    hri_eic_set_CTRLA_CKSEL_bit(EIC);
    // Level rather than edge sensing: while an IRQ is still pending the pin stays high, so with an edge the next one
    // would never fire the line
    hri_eic_write_CONFIG_SENSE3_bf(EIC, AT86_IRQ_EXTINT / 8, EIC_CONFIG_SENSE3_HIGH_Val);
    hri_eic_clear_INTEN_reg(EIC, AT86_IRQ_MASK);
    hri_eic_clear_INTFLAG_reg(EIC, AT86_IRQ_MASK);
    hri_eic_set_CTRLA_ENABLE_bit(EIC);

    gpio_set_pin_function(UHF_IRQ, PINMUX_PA27A_EIC_EXTINT11);

    // The ISR uses the FreeRTOS API, so it can't be above configMAX_SYSCALL_INTERRUPT_PRIORITY
    NVIC_SetPriority(EIC_11_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_ClearPendingIRQ(EIC_11_IRQn);
    NVIC_EnableIRQ(EIC_11_IRQn);
}

/**
 * \fn at86_irq_wait
 *
 * \brief Waits for the radio's IRQ pin, then reads (and so clears) its IRQS registers. Only one task may wait at a time,
 * and only after the scheduler has started.
 *
 * \param h radio handle, only used for register access
 * \param irqs filled with the RF09 and BBC0 IRQS registers if the pin went high
 * \param timeout_ms how long to wait for the pin
 *
 * \returns bool true if the pin went high and the registers were read
 */
bool at86_irq_wait(struct at86rf215 *h, at86_irqs_t *irqs, uint32_t timeout_ms) {
    // Drop a wakeup left over from an earlier wait that timed out just as the line fired
    xSemaphoreTake(irq_semaphore, 0);
    // An IRQ that is already pending still fires the line here, since the pin stays high until the registers are read
    hri_eic_clear_INTFLAG_reg(EIC, AT86_IRQ_MASK);
    hri_eic_set_INTEN_reg(EIC, AT86_IRQ_MASK);
    const bool fired = xSemaphoreTake(irq_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    hri_eic_clear_INTEN_reg(EIC, AT86_IRQ_MASK);
    if (!fired) {
        return false;
    }

    // All four registers are read so the pin is released; PVDX only uses the sub-GHz radio (RF09/BBC0)
    uint8_t unused = 0;
    if (at86rf215_reg_read_8(h, &irqs->rf, REG_RF09_IRQS) != AT86RF215_OK ||
        at86rf215_reg_read_8(h, &irqs->bbc, REG_BBC0_IRQS) != AT86RF215_OK ||
        at86rf215_reg_read_8(h, &unused, REG_RF24_IRQS) != AT86RF215_OK ||
        at86rf215_reg_read_8(h, &unused, REG_BBC1_IRQS) != AT86RF215_OK) {
        warning("at86_irq: failed to read the IRQS registers\n");
        return false;
    }
    return true;
}

void EIC_11_Handler(void) {
    hri_eic_clear_INTEN_reg(EIC, AT86_IRQ_MASK);
    hri_eic_clear_INTFLAG_reg(EIC, AT86_IRQ_MASK);
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(irq_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
