    .syntax unified
    .cpu cortex-m4
    .thumb
    
    .globl _start
    .section .isr_vector, "a", %progbits
_start:
    .word _estack            /* Top of stack -- Defined in the linker file*/
    .word bootloader_rst     /* Reset Handler -- First code that runs */
    /* Other handlers can be added to the vector table if needed, but probably not needed */

    .section .text
bootloader_rst:
    /* Initialize data and bss sections */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =.data
    movs r3, #0
copy_loop:
    cmp r0, r1
    itt lo
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo copy_loop

    ldr r0, =_sbss
    ldr r1, =_ebss
zero_loop:
    cmp r0, r1
    it lo
    strlo r3, [r0], #4
    blo zero_loop

    /* Call the main function (assuming you have one) */
    bl main

    /* If main returns, loop forever */
    b .
