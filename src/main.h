#include "SEGGER_RTT.h"
#include "cosmicmonkey_task.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "logging.h"
#include "rtos_start.h"
#include "shell_task.h"
#include "watchdog_task.h"
#include "task_manager.h"

#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>
#include <string.h>

/*
Standard definitions for main.c
*/

#define BOOTLOADER_MAGIC_NUMBER_ADDRESS (BKUPRAM_ADDR + 0x0)
#define BOOTLOADER_MAGIC_NUMBER_VALUE (0x50564458UL) // ASCII for 'PVDX'

/*
Compilation guards to make sure that compilation is being done with the correct flags and correct compiler versions
If you want to get rid of the red squiggly lines:
- set C standard to GNU99 in the C/C++ extension settings
- Add "-DDEVBUILD" to the IntelliSense settings as a compiler argument
*/

// Check GNU 99 standard
#if __STDC_VERSION__ != 199901L
    #error "This program needs to be compiled with the GNU99 Standard"
#endif

// Check that at least one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE)
    #error "Build type flag not set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
// Check that at most one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if defined(DEVBUILD) && defined(UNITTEST)
    #error "Multiple build type flags set! (DEVBUILD && UNITTEST) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(DEVBUILD) && defined(RELEASE)
    #error "Multiple build type flags set! (DEVBUILD && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(UNITTEST) && defined(RELEASE)
    #error "Multiple build type flags set! (UNITTEST && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif

#ifndef GIT_BRANCH_NAME
    #define GIT_BRANCH_NAME "Unspecified"
#endif

#ifndef GIT_COMMIT_HASH
    #define GIT_COMMIT_HASH "Unspecified"
#endif

// Defines for printing out the build version
#if defined(DEVBUILD)
    #define BUILD_TYPE "Development Build"
#endif
#if defined(UNITTEST)
    #define BUILD_TYPE "Unit Test Build"
#endif
#if defined(RELEASE)
    #define BUILD_TYPE "Release Build"
#endif

// Define build date
#define BUILD_DATE __DATE__

// Define build timame
#define BUILD_TIME __TIME__

// Defines which should never actually execute, but are worth having for IDE reasons
// (so that the IDE knows what flags to expect and doesn't throw a fit)
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE) // No build flags set
    #define BUILD_TYPE "Unspecified"
    #define DEVBUILD
#endif

int main(void);
void hardware_init(void);