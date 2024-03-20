// Configuration stuff

#define APPLICATION_START_ADDRESS (0x2000UL) // Start address of the application

#define BACKUP_RAM (0x47000000UL)
#define MAGIC_NUMBER_ADDRESS (BACKUP_RAM + 0x0)
#define MAGIC_NUMBER (0x50564458UL)  // ASCII for 'PVDX'
#define FAILURE_NUMBER (0xBADC0DEUL) // "Bad Code"

#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08) // VTOR: Vector Table Offset Register (where the processor will reset from)

#define SCB_VTOR_TBLOFF_Msk (0xFFFFFF80UL) // Mask off the last 7 bits (128 bytes alignment)

#define FLASH_START (0UL)
#define FLASH_SIZE (0x100000UL) // 1MB
#define FLASH_END (FLASH_START + FLASH_SIZE)

#define RAM_START (0x20000000UL)
#define RAM_SIZE (0x40000UL) // 256KB
#define RAM_END (RAM_START + RAM_SIZE)

// Sanity checks on values
#if FLASH_START > APPLICATION_START
    #error "APPLICATION_START must be within FLASH (FLASH_START > APPLICATION_START)"
#endif

#if APPLICATION_START > FLASH_END
    #error "APPLICATION_START must be within FLASH (APPLICATION_START > FLASH_END)"
#endif

__attribute__((noreturn)) void bootloader();
__attribute__((noreturn)) void transfer_to_application();