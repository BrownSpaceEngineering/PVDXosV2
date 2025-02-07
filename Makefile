###############################################################################
##################### ADDING SOMETHING?  READ THIS FIRST! #####################
###############################################################################
###
###  When adding a new C file to the source, you must do 2 things:
###    - Add the file to the OBJS list below (with a .o extension)
###    - If it is in a new directory, Add the directory to the EXTRA_VPATH list below (with a .c extension)
###  Remember to add the trailing \ to the end of each line!
###
###############################################################################
###############################################################################
###############################################################################



### ALL C FILES SHOULD HAVE AN OBJECT FILE LISTED HERE ###
export OBJS := \
../src/main.o \
../src/tasks/heartbeat/heartbeat_main.o \
../src/tasks/watchdog/watchdog_main.o \
../src/tasks/watchdog/watchdog_helpers.o \
../src/misc/printf/SEGGER_RTT.o \
../src/misc/printf/SEGGER_RTT_printf.o \
../src/misc/rtos_support/rtos_static_memory.o \
../src/misc/rtos_support/rtos_stack_overflow.o \
../src/tasks/cosmic_monkey/cosmicmonkey_main.o \
../src/misc/logging/logging.o \
../src/misc/exception_handlers/default_handler.o \
../src/misc/exception_handlers/specific_handlers.o \
../src/tasks/display/display_ssd1362.o \
../src/tasks/display/image_buffers/image_buffer_BrownLogo.o \
../src/tasks/display/image_buffers/image_buffer_PVDX.o \
../src/tasks/task_manager/task_manager.o \
../src/tasks/shell/shell_main.o \
../src/tasks/shell/shell_helpers.o \
../src/tasks/shell/shell_commands.o \
../src/tasks/rm3100/rm3100.o \
../src/tasks/arducam/arducam.o \

### ALL DIRECTORIES WITH SOURCE FILES MUST BE LISTED HERE ###
### THESE ARE WRITTEN RELATIVE TO THE ./ASF/gcc/Makefile FILE ###
export EXTRA_VPATH := \
../../src \
../../src/tasks \
../../src/tasks/heartbeat \
../../src/tasks/watchdog \
../../src/misc \
../../src/misc/printf \
../../src/misc/rtos_support \
../../src/misc/hardware_watchdog_utils \
../../src/tasks/cosmic_monkey \
../../src/misc/logging \
../../src/misc/exception_handlers \
../../src/tasks/display \
../../src/tasks/task_manager \
../../src/tasks/display/image_buffers \
../../src/tasks/shell \
../../src/tasks/rm3100 \
../../src/tasks/arducam \

###############################################################################
###############################################################################
###############################################################################


#Technical stuff

#Makefile usually uses /bin/sh to evaluate commands, so we need to change it to /bin/bash
#To allow for the if statement in the connect target to execute correctly
SHELL := /bin/bash 

#Path to the child makefile
#This makefile is used to build the ASF files, and is provided by Atmel Start
CHILD_MAKEFILE_PATH := ./ASF/gcc

# Ensure that MK_DIR is set even when none of the checks are hit
export MK_DIR := mkdir -p

# Use gsed on macOS
ifeq ($(shell uname), Darwin)
	SED = gsed
else
	SED = sed
endif

# Get git branch name and commit hash
GIT_AVAILABLE := $(shell git rev-parse > /dev/null 2>&1; echo $$?)
# ^ This is set to the return code of 'git rev-parse' (0 if successful, 1 if not)

ifeq ($(GIT_AVAILABLE),0) # Zero case means we *are* in a git repo
    GIT_BRANCH_NAME := \"$(shell git rev-parse --abbrev-ref HEAD)\"
    GIT_COMMIT_HASH := \"$(shell git rev-parse HEAD | tail -c 8)\"
else
    $(warning Not in a Git repository, proceeding without Git information)
    GIT_BRANCH_NAME := \"NONE\"
    GIT_COMMIT_HASH := \"NONE\"
endif


# Compiler flags
# Include git branch and commit hash in the build
CFLAGS += -D'GIT_BRANCH_NAME="$(GIT_BRANCH_NAME)"' -D'GIT_COMMIT_HASH="$(GIT_COMMIT_HASH)"'

# Robust error checking
CFLAGS += -Wextra -Werror -Werror=maybe-uninitialized
CFLAGS += -Wshadow -Wnull-dereference -Wduplicated-cond -Wlogical-op -Werror=return-type -Wfloat-equal
CFLAGS += -Wdangling-else -Wtautological-compare
CFLAGS += -fwrapv # Enable fwrapv (wrap on overflow of signed integers) just to be safe
CFLAGS += -fsigned-char # Ensure that char is signed as your average c programmer might expect -- it's actually default unsigned on arm!

# Disable warnings for unused parameters due to ASF functions having unused parameters
CFLAGS += -Wno-unused-parameter #Because some ASF functions have unused parameters, supress this warning

# Build-specific flags
CFLAGS_DEV := -DDEVBUILD
CFLAGS_UNITTEST := -DUNITTEST
CFLAGS_RELEASE := -DRELEASE

### All these variables are exported to the child makefile, and affect its behavior ###
export SUB_DIRS := $(shell for dir in $(EXTRA_VPATH); do echo $$dir | $(SED) 's|\.\./||g'; done)

export DIR_INCLUDES := $(foreach dir,$(EXTRA_VPATH),-I"$(dir)" )

export OBJS_AS_ARGS := $(foreach obj,$(OBJS),$(patsubst ../%,%,$(obj)))

export DEPS_AS_ARGS := $(patsubst %.o,%.d,$(OBJS_AS_ARGS))


.PHONY: all dev release test clean connect update_asf flash_bootloader

# Default target
all: dev

dev:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) CFLAGS=" $(CFLAGS_DEV) $(CFLAGS)" \
	&& cp -f ./ASF/gcc/PVDXos.elf ./ \
	&& echo " --- Finished Building PVDXos.elf --- " \
	&& echo " ---    THIS IS THE DEV BUILD!    --- "

release: clean #Might as well clean before compiling the release build
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) CFLAGS=" $(CFLAGS_RELEASE) $(CFLAGS)" \
	&& cp -f ./ASF/gcc/PVDXos.elf ./ \
	&& echo " --- Finished Building PVDXos.elf --- " \
	&& echo " ---  THIS IS THE RELEASE BUILD!  --- "

test:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) CFLAGS=" $(CFLAGS_UNITTEST) $(CFLAGS)" \
	&& cp -f ./ASF/gcc/PVDXos.elf ./ \
	&& echo " --- Finished Building PVDXos.elf --- " \
	&& echo " --- THIS IS THE UNIT TEST BUILD! --- "

# Clean target
clean:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) clean \
	&& rm -f ./PVDXos.elf \
	&& echo " --- Cleaned Build Files --- "

# Connects to remote target, loads program, and sets breakpoint at main
connect:
ifeq (,$(findstring microsoft,$(shell uname -r))) #Detects a WSL kernel name, and runs a WSL-specific command for connecting to the GDB server
	@gdb -ex "target remote localhost:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "b main" -ex "continue" ./PVDXos.elf
else ifeq (,$(findstring generic,$(shell uname -r)))
	@gdb-multiarch -ex "target remote localhost:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "b main" -ex "continue" ./PVDXos.elf
else #Run the windows-specific command
	@hostname=$(shell hostname) && \
	gdb-multiarch -ex "target remote $$hostname.local:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "b main" -ex "continue" ./PVDXos.elf
endif

#Builds the bootloader, then flashes it to the board. Make connect needs to be run after this.
flash_bootloader:
	$(MAKE) -C ./bootloader clean
	$(MAKE) -C ./bootloader # Builds the bootloader
ifeq (,$(findstring microsoft,$(shell uname -r))) #Detects a WSL kernel name, and runs a WSL-specific command for connecting to the GDB server
	@gdb -ex "target remote localhost:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "set confirm off" -ex "add-symbol-file PVDXos.elf" -ex "set confirm on" ./bootloader/bootloader.elf
else #Run the windows-specific command
	@hostname=$(shell hostname) && \
	gdb-multiarch -ex "target remote $$hostname.local:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "set confirm off" -ex "add-symbol-file PVDXos.elf" -ex "set confirm on" ./bootloader/bootloader.elf
endif

# When updating the ASF configuration, this must be run once in order to automatically integrate the new ASF config
# Hopefully nobody ever needs to touch this, but you can add to it if you want to automatically trigger an action when the ASF is updated
# The worst part of this is step 6, making text modifications to the stock ASF Makefile
# IF YOU MODIFY STEP 6, PLEASE MAKE SURE YOU KNOW WHAT YOU'RE DOING
update_asf:
	@if [ ! -f ASF.atzip ]; then \
		echo "ASF.atzip not found in the current directory! (Make sure spelling and capitalization is exact)"; \
		echo " --- Operation Aborted --- "; \
		exit 1; \
	fi;
	@read -p "WARNING -- Are you sure you want to REPLACE the ASF with ASF.atzip? [Y/N] " confirm; \
	if [ "$$confirm" != "y" ] && [ "$$confirm" != "Y" ]; then \
		echo " --- Operation aborted --- "; \
		exit 1; \
	fi;
	@echo "(0) Starting..." \
	&& rm -rf ./ASF \
	&& echo "(1) ASF dir removed" \
	&& rm -f ./atmel_start_config.atstart \
	&& echo "(2) ASF config removed" \
	&& mkdir -p ASF \
	&& echo "(3) ASF dir re-Created" \
	&& unzip -q ASF.atzip -d ASF \
	&& echo "(4) ASF unzipped" \
	&& cp -f ./ASF/atmel_start_config.atstart ./ \
	&& echo "(5) ASF config lifted" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.d)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.d))/g' ./ASF/gcc/Makefile \
	&& echo "(6.1) ASF Makefile: GCC dependency filepaths corrected" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.o)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.o))/g' ./ASF/gcc/Makefile \
	&& echo "(6.2) ASF Makefile: GCC object filepaths corrected" \
	&& $(SED) -i 's/\$$@/\$$(strip \$$(patsubst ..\/%, %, $$@))/g' ./ASF/gcc/Makefile \
	&& echo "(6.3) ASF Makefile: GCC output filepaths corrected" \
	&& $(SED) -i '/main/d' ./ASF/gcc/Makefile \
	&& echo "(6.4) ASF Makefile: References to ASF main.c removed" \
	&& $(SED) -i 's/-DDEBUG/$$(CFLAGS) -D__FILENAME__=\\"$$\(notdir $$<)\\"/g' ./ASF/gcc/Makefile \
	&& echo "(6.5) ASF Makefile: CFLAGS and __FILENAME__ hook injected" \
	&& $(SED) -i 's/AtmelStart/PVDXos/g' ./ASF/gcc/Makefile \
	&& echo "(6.6) ASF Makefile: Project name updated to PVDXos" \
	&& rm -f ./ASF/main.c \
	&& echo "(7) ASF main.c removed" \
	&& $(SED) -i 's|// <h> Basic|#define configSUPPORT_STATIC_ALLOCATION 1\n#define INCLUDE_xQueueGetMutexHolder 1|' ./ASF/config/FreeRTOSConfig.h \
	&& echo "(8.1) ASF FreeRTOSConfig.h: Static allocation & QueueGetMutexHolder enabled" \
	&& $(SED) -i 's|#define INCLUDE_uxTaskGetStackHighWaterMark 0|#define INCLUDE_uxTaskGetStackHighWaterMark 1|' ./ASF/config/FreeRTOSConfig.h \
	&& echo "(8.2) ASF FreeRTOSConfig.h: Task stack high watermark function enabled" \
	&& $(SED) -i 's|#define configCHECK_FOR_STACK_OVERFLOW 1|#define configCHECK_FOR_STACK_OVERFLOW 2|' ./ASF/config/FreeRTOSConfig.h \
	&& echo "(8.3) ASF FreeRTOSConfig.h: Task stack overflow checking upgraded to type 2 (higher accuracy)" \
	&& $(SED) -i 's|ORIGIN = 0x00000000, LENGTH = 0x00100000|ORIGIN = 0x00002000, LENGTH = 0x000FE000|' ./ASF/samd51a/gcc/gcc/samd51p20a_flash.ld \
	&& echo "(9) ASF Linker Script: Flash memory region updated to exclude bootloader" \
	&& find ./ASF -type f -newermt now -exec touch {} + \
	&& echo "(10) Timestamps in future updated to present" \
	&& echo " --- Finished Integrating ASF --- "