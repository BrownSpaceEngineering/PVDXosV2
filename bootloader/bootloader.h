// Configuration stuff

#define APPLICATION_START_ADDRESS (0x10000UL) // Start address of the application

#define MAGIC_NUMBER_ADDRESS (BACKUP_RAM + 0x00)
#define MAGIC_NUMBER (0x50564458UL)  // ASCII for 'PVDX'
#define FAILURE_NUMBER (0xBADC0DEUL) // "Bad Code"

// Select definitions copied from ASF files (do not want to import the whole thing for size reasons)
#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08) // VTOR: Vector Table Offset Register (where the processor will reset from)

#define SCB_VTOR_TBLOFF_Msk (0xFFFFFF80UL) // Mask off the last 7 bits (128 bytes alignment)

#define BACKUP_RAM (0x47000000UL)
