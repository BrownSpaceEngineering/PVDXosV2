// Select definitions copied from ASF files (do not want to import the whole thing for size reasons)
#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08)  // VTOR: Vector Table Offset Register (where the processor will reset from)
#define SCB_AIRCR (SCB_BASE + 0x0C) // AIRCR: Application Interrupt and Reset Control Register (contains reset bit)

#define AIRCR_SYSRESETREQ (1UL << 2) // SYSRESETREQ: System Reset Request bit

#define BACKUP_RAM (0x47000000UL)
#define MAGIC_NUMBER_ADDRESS (BACKUP_RAM + 0x00)
#define MAGIC_NUMBER (0x50564458UL) // ASCII for 'PVDX'

#define APPLICATION_START_ADDRESS (0x10000UL) // Start address of the application
