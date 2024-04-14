#include "default_handler.h"
#include "logging.h"

// Implementation of Cortex-M4 core handlers
void NonMaskableInt_Handler(void) {
    //warning("NMI Handler Entered\n");
    PVDX_default_handler();
}
void HardFault_Handler(void) {
    //warning("HardFault Handler Entered\n");
    PVDX_default_handler();
}
void MemManagement_Handler(void) {
    //warning("MemManagement Handler Entered\n");
    PVDX_default_handler();
}
void BusFault_Handler(void) {
    //warning("BusFault Handler Entered\n");
    PVDX_default_handler();
}
void UsageFault_Handler(void) {
    //warning("UsageFault Handler Entered\n");
    PVDX_default_handler();
}

/* --- ALREADY DEFINED IN "port.c"
void SVCall_Handler(void) {
    PVDX_default_handler();
}
*/

void DebugMonitor_Handler(void) {
    PVDX_default_handler();
}

/* --- ALREADY DEFINED IN "port.c"
void PendSV_Handler(void) {
    PVDX_default_handler();
}
*/

/* --- ALREADY DEFINED IN "port.c"
void SysTick_Handler(void) {
    PVDX_default_handler();
}
*/

// Implementation of peripherals handlers
void PM_Handler(void) {
    PVDX_default_handler();
}
void MCLK_Handler(void) {
    PVDX_default_handler();
}
void OSCCTRL_0_Handler(void) {
    PVDX_default_handler();
}
void OSCCTRL_1_Handler(void) {
    PVDX_default_handler();
}
void OSCCTRL_2_Handler(void) {
    PVDX_default_handler();
}
void OSCCTRL_3_Handler(void) {
    PVDX_default_handler();
}
void OSCCTRL_4_Handler(void) {
    PVDX_default_handler();
}
void OSC32KCTRL_Handler(void) {
    PVDX_default_handler();
}
void SUPC_0_Handler(void) {
    PVDX_default_handler();
}
void SUPC_1_Handler(void) {
    PVDX_default_handler();
}
void WDT_Handler(void) {
    PVDX_default_handler();
}
void RTC_Handler(void) {
    PVDX_default_handler();
}
void EIC_0_Handler(void) {
    PVDX_default_handler();
}
void EIC_1_Handler(void) {
    PVDX_default_handler();
}
void EIC_2_Handler(void) {
    PVDX_default_handler();
}
void EIC_3_Handler(void) {
    PVDX_default_handler();
}
void EIC_4_Handler(void) {
    PVDX_default_handler();
}
void EIC_5_Handler(void) {
    PVDX_default_handler();
}
void EIC_6_Handler(void) {
    PVDX_default_handler();
}
void EIC_7_Handler(void) {
    PVDX_default_handler();
}
void EIC_8_Handler(void) {
    PVDX_default_handler();
}
void EIC_9_Handler(void) {
    PVDX_default_handler();
}
void EIC_10_Handler(void) {
    PVDX_default_handler();
}
void EIC_11_Handler(void) {
    PVDX_default_handler();
}
void EIC_12_Handler(void) {
    PVDX_default_handler();
}
void EIC_13_Handler(void) {
    PVDX_default_handler();
}
void EIC_14_Handler(void) {
    PVDX_default_handler();
}
void EIC_15_Handler(void) {
    PVDX_default_handler();
}
void FREQM_Handler(void) {
    PVDX_default_handler();
}
void NVMCTRL_0_Handler(void) {
    PVDX_default_handler();
}
void NVMCTRL_1_Handler(void) {
    PVDX_default_handler();
}
void DMAC_0_Handler(void) {
    PVDX_default_handler();
}
void DMAC_1_Handler(void) {
    PVDX_default_handler();
}
void DMAC_2_Handler(void) {
    PVDX_default_handler();
}
void DMAC_3_Handler(void) {
    PVDX_default_handler();
}
void DMAC_4_Handler(void) {
    PVDX_default_handler();
}
void EVSYS_0_Handler(void) {
    PVDX_default_handler();
}
void EVSYS_1_Handler(void) {
    PVDX_default_handler();
}
void EVSYS_2_Handler(void) {
    PVDX_default_handler();
}
void EVSYS_3_Handler(void) {
    PVDX_default_handler();
}
void EVSYS_4_Handler(void) {
    PVDX_default_handler();
}
void PAC_Handler(void) {
    PVDX_default_handler();
}

/* --- ALREADY DEFINED IN "hri_ramecc_d51.h"
void RAMECC_Handler(void) {
    PVDX_default_handler();
}
*/

void SERCOM0_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM0_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM0_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM0_3_Handler(void) {
    PVDX_default_handler();
}
void SERCOM1_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM1_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM1_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM1_3_Handler(void) {
    PVDX_default_handler();
}
void SERCOM2_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM2_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM2_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM2_3_Handler(void) {
    PVDX_default_handler();
}
void SERCOM3_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM3_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM3_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM3_3_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_SERCOM4
void SERCOM4_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM4_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM4_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM4_3_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_SERCOM5
void SERCOM5_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM5_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM5_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM5_3_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_SERCOM6
void SERCOM6_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM6_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM6_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM6_3_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_SERCOM7
void SERCOM7_0_Handler(void) {
    PVDX_default_handler();
}
void SERCOM7_1_Handler(void) {
    PVDX_default_handler();
}
void SERCOM7_2_Handler(void) {
    PVDX_default_handler();
}
void SERCOM7_3_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_CAN0
void CAN0_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_CAN1
void CAN1_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_USB
void USB_0_Handler(void) {
    PVDX_default_handler();
}
void USB_1_Handler(void) {
    PVDX_default_handler();
}
void USB_2_Handler(void) {
    PVDX_default_handler();
}
void USB_3_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_GMAC
void GMAC_Handler(void) {
    PVDX_default_handler();
}
#endif
void TCC0_0_Handler(void) {
    PVDX_default_handler();
}
void TCC0_1_Handler(void) {
    PVDX_default_handler();
}
void TCC0_2_Handler(void) {
    PVDX_default_handler();
}
void TCC0_3_Handler(void) {
    PVDX_default_handler();
}
void TCC0_4_Handler(void) {
    PVDX_default_handler();
}
void TCC0_5_Handler(void) {
    PVDX_default_handler();
}
void TCC0_6_Handler(void) {
    PVDX_default_handler();
}
void TCC1_0_Handler(void) {
    PVDX_default_handler();
}
void TCC1_1_Handler(void) {
    PVDX_default_handler();
}
void TCC1_2_Handler(void) {
    PVDX_default_handler();
}
void TCC1_3_Handler(void) {
    PVDX_default_handler();
}
void TCC1_4_Handler(void) {
    PVDX_default_handler();
}
void TCC2_0_Handler(void) {
    PVDX_default_handler();
}
void TCC2_1_Handler(void) {
    PVDX_default_handler();
}
void TCC2_2_Handler(void) {
    PVDX_default_handler();
}
void TCC2_3_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_TCC3
void TCC3_0_Handler(void) {
    PVDX_default_handler();
}
void TCC3_1_Handler(void) {
    PVDX_default_handler();
}
void TCC3_2_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_TCC4
void TCC4_0_Handler(void) {
    PVDX_default_handler();
}
void TCC4_1_Handler(void) {
    PVDX_default_handler();
}
void TCC4_2_Handler(void) {
    PVDX_default_handler();
}
#endif
void TC0_Handler(void) {
    PVDX_default_handler();
}
void TC1_Handler(void) {
    PVDX_default_handler();
}
void TC2_Handler(void) {
    PVDX_default_handler();
}
void TC3_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_TC4
void TC4_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_TC5
void TC5_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_TC6
void TC6_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_TC7
void TC7_Handler(void) {
    PVDX_default_handler();
}
#endif
void PDEC_0_Handler(void) {
    PVDX_default_handler();
}
void PDEC_1_Handler(void) {
    PVDX_default_handler();
}
void PDEC_2_Handler(void) {
    PVDX_default_handler();
}
void ADC0_0_Handler(void) {
    PVDX_default_handler();
}
void ADC0_1_Handler(void) {
    PVDX_default_handler();
}
void ADC1_0_Handler(void) {
    PVDX_default_handler();
}
void ADC1_1_Handler(void) {
    PVDX_default_handler();
}
void AC_Handler(void) {
    PVDX_default_handler();
}
void DAC_0_Handler(void) {
    PVDX_default_handler();
}
void DAC_1_Handler(void) {
    PVDX_default_handler();
}
void DAC_2_Handler(void) {
    PVDX_default_handler();
}
void DAC_3_Handler(void) {
    PVDX_default_handler();
}
void DAC_4_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_I2S
void I2S_Handler(void) {
    PVDX_default_handler();
}
#endif
void PCC_Handler(void) {
    PVDX_default_handler();
}
void AES_Handler(void) {
    PVDX_default_handler();
}
void TRNG_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_ICM
void ICM_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_PUKCC
void PUKCC_Handler(void) {
    PVDX_default_handler();
}
#endif
void QSPI_Handler(void) {
    PVDX_default_handler();
}
#ifdef ID_SDHC0
void SDHC0_Handler(void) {
    PVDX_default_handler();
}
#endif
#ifdef ID_SDHC1
void SDHC1_Handler(void) {
    PVDX_default_handler();
}
#endif
