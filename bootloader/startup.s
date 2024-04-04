    .syntax unified
    .cpu cortex-m4
    .thumb
    
    .globl _start
    .section .isr_vector, "a", %progbits
_start:
    .word _estack            /* Top of stack -- Defined in the linker file*/
    .word bootloader_rst     /* Reset Handler -- First code that runs */
    .word bootloader_hardfault     /* Hard Fault Handler */
    .word bootloader_hardfault     /* Add this back in two more times, since sometimes the watchdog reset leads to the hardfault handler getting called */
    /* Other handlers can be added to the vector table if needed, but probably not needed */

    .section .text
bootloader_hardfault:
    /* Hard fault handler */
    nop
    nop
    /* For now, just fallthrough to the reset handler */
bootloader_rst:
    /* Initialize data section */
    ldr r0, =_sdata /* points to RAM */
    ldr r1, =_edata /* points to RAM */
    ldr r2, =_sdata_flash /* points to FLASH */
data_copy_loop:
    cmp r0, r1
    itt lo
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo data_copy_loop

    /* Done copying the .data section, now zero the .bss section */

    ldr r0, =_sbss /* points to RAM */
    ldr r1, =_ebss /* points to RAM */
    movs r3, #0
zero_loop:
    cmp r0, r1
    it lo
    strlo r3, [r0], #4
    blo zero_loop

    /* Call the bootloader function */
    bl bootloader

    /* If the bootloader returns, loop forever (certified fucky-wucky situation) */
deathloop:
    b deathloop
