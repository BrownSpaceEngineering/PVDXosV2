/* Define memory regions */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1M    /* 1MB Flash */
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K   /* 256KB RAM */
}

/* Define the entry point */
ENTRY(_start)

SECTIONS
{
  /* Place the vector table at the start of FLASH */
  .isr_vector :
  {
    . = 0x0
    KEEP(*(.isr_vector)) /* KEEP() Ensures vector table is not optimized out */
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
    _sdata = .;         /* Create a symbol marking the start of the .data section */
    *(.data*)           /* All initialized data */
    _edata = .;         /* Define a symbol marking the end of the .data section */
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
    . = . + 0x400;      /* Adjust stack size as needed */
    _estack = .;        /* Define stack top symbol */
  } > RAM
}
