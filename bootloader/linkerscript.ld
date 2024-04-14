/* Define memory regions */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x2000    /* 0x2000 (8KiB) of flash reserved for bootloader */
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K   /* 256KB RAM */
}

/* Define the entry point */
ENTRY(_start)

SECTIONS
{
  /* Place the vector table at the start of FLASH */
  .isr_vector :
  {
    KEEP(*(.isr_vector*))
    . = ALIGN(4);
  } > FLASH

  /* Code and RO data */
  .text :
  {
    *(.text*)           /* All code */
    *(.rodata*)         /* Read-only data (constants) */
    . = ALIGN(4);
  } > FLASH

  /* Initialized data */
  .data : AT(ADDR(.text) + SIZEOF(.text))
  {
    _sdata_flash = LOADADDR(.data);                  /* Create a symbol marking the start of the .data section in FLASH */
    _sdata = .;                                      /* Create a symbol marking the start of the .data section in RAM*/
    *(.data*)                                        /* All initialized data */
    _edata = .;                                      /* Create a symbol marking the end of the .data section in RAM*/
    _edata_flash = LOADADDR(.data) + SIZEOF(.data); /* Create a symbol marking the end of the .data section in FLASH */
    . = ALIGN(4);
  } > RAM

  /* Uninitialized data (zero initialized) */
  .bss :
  {
    _sbss = .;          /* Symbol marking the start of the .bss section */
    *(.bss*)
    *(COMMON)
    _ebss = .;          /* Symbol marking the end of the .bss section */
    . = ALIGN(4);
  } > RAM

  /* Stack space */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + 0x10000;      /* No memory issues here, just define a big 10KiB stack */
    _estack = .;        /* Define stack top symbol */
  } > RAM
}
