MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x10000
  RAM   (xrw) : ORIGIN = 0x2003E000, LENGTH = 0x1000
}

ENTRY(bootloader)

SECTIONS
{
  /* Vector table goes at the start of flash */
  .vectors : ALIGN(4)
  {
    KEEP(*(.vectors))
  } > FLASH

  .text : ALIGN(4)
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
  } > FLASH

  .data : ALIGN(4)
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH

  .bss : ALIGN(4)
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM

  /* .data section needs copy info */
  _sidata = LOADADDR(.data);
}
