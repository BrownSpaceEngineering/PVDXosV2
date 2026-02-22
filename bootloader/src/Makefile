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
../src/bootloader.o \
../src/mram.o

### ALL DIRECTORIES WITH SOURCE FILES MUST BE LISTED HERE ###
### THESE ARE WRITTEN RELATIVE TO THE ./ASF/gcc/Makefile FILE ###
export EXTRA_VPATH := \
../../src

###############################################################################
###############################################################################
###############################################################################

#Technical stuff

#Makefile usually uses /bin/sh to evaluate commands, so we need to change it to /bin/bash
#To allow for the if statement in the connect target to execute correctly
SHELL := /bin/bash

#Path to the child makefile
#This makefile is used to build the ASF files, and is provided by Atmel Start
CHILD_MAKEFILE_PATH := ../ASF/gcc

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




### All these variables are exported to the child makefile, and affect its behavior ###
export SUB_DIRS := $(shell for dir in $(EXTRA_VPATH); do echo $$dir | $(SED) 's|\.\./||g'; done)

export DIR_INCLUDES := $(foreach dir,$(EXTRA_VPATH),-I"$(dir)" )

export OBJS_AS_ARGS := $(foreach obj,$(OBJS),$(patsubst ../%,%,$(obj)))

export DEPS_AS_ARGS := $(patsubst %.o,%.d,$(OBJS_AS_ARGS))

.PHONY: all clean update_bootloader_asf

# Default target
all: clean #Might as well clean before compiling the release build
	@for i in $$(seq 1 3); do \
		echo $$i \
		&& $(MAKE) -C $(CHILD_MAKEFILE_PATH) CFLAGS=" $(CFLAGS) -DBOOTLOADER_$$i" LINKER_SCRIPT="../../src/bootloader_flash$$i.ld" \
		&& cp -f ../ASF/gcc/PVDXos_bootloader.bin ./bootloader$$i.bin \
		&& cp -f ../ASF/gcc/PVDXos_bootloader.elf ./bootloader$$i.elf; \
	done \
	&& echo " ---  Finished Building PVDXos Bootloader  --- "

# Clean target
clean:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) clean \
	&& rm -f *.o *.elf *.bin *.hex *.map \
	&& echo " --- Cleaned Bootloader Build Files --- "

# When updating the ASF configuration, this must be run once in order to automatically integrate the new ASF config
# Hopefully nobody ever needs to touch this, but you can add to it if you want to automatically trigger an action when the ASF is updated
# The worst part of this is step 6, making text modifications to the stock ASF Makefile
# IF YOU MODIFY STEP 6, PLEASE MAKE SURE YOU KNOW WHAT YOU'RE DOING
update_bootloader_asf:
	@if [ ! -f ../ASF.atzip ]; then \
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
	&& rm -rf ../ASF \
	&& echo "(1) ASF dir removed" \
	&& rm -f ../atmel_start_config.atstart \
	&& echo "(2) ASF config removed" \
	&& mkdir -p ../ASF \
	&& echo "(3) ASF dir re-Created" \
	&& unzip -q ../ASF.atzip -d ../ASF \
	&& echo "(4) ASF unzipped" \
	&& cp -f ../ASF/atmel_start_config.atstart ./ \
	&& echo "(5) ASF config lifted" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.d)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.d))/g' ../ASF/gcc/Makefile \
	&& echo "(6.1) ASF Makefile: GCC dependency filepaths corrected" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.o)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.o))/g' ../ASF/gcc/Makefile \
	&& echo "(6.2) ASF Makefile: GCC object filepaths corrected" \
	&& $(SED) -i 's/\$$@/\$$(strip \$$(patsubst ..\/%, %, $$@))/g' ../ASF/gcc/Makefile \
	&& echo "(6.3) ASF Makefile: GCC output filepaths corrected" \
	&& $(SED) -i '/main/d' ../ASF/gcc/Makefile \
	&& echo "(6.4) ASF Makefile: References to ASF main.c removed" \
	&& $(SED) -i 's/-DDEBUG/$$(CFLAGS) -D__FILENAME__=\\"$$\(notdir $$<)\\"/g' ../ASF/gcc/Makefile \
	&& echo "(6.5) ASF Makefile: CFLAGS and __FILENAME__ hook injected" \
	&& $(SED) -i 's/AtmelStart/PVDXos_bootloader/g' ../ASF/gcc/Makefile \
	&& echo "(6.6) ASF Makefile: Project name updated to PVDXos_bootloader" \
	&& rm -f ../ASF/main.c \
	&& echo "(7) ASF main.c removed" \
	&& $(SED) -i 's|"\.\./samd51a/gcc/gcc/samd51p20a_flash\.ld"|"$$(LINKER_SCRIPT)"|' ../ASF/gcc/Makefile \
	&& echo "(9) ASF Linker Script: ASF Makefile updated to use custom flash script" \
	&& find ../ASF -type f -newermt now -exec touch {} + \
	&& echo "(10) Timestamps in future updated to present" \
	&& echo " --- Finished Integrating ASF --- "

# # previous stuff 

# # === CONFIG ===
# TARGET1 = bootloader1
# TARGET2 = bootloader2
# TARGET3 = bootloader3
# MCU = cortex-m4
# FPU = fpv4-sp-d16
# FLOAT_ABI = hard
# LD_SCRIPT1 = bootloader_flash1.ld
# LD_SCRIPT2 = bootloader_flash2.ld
# LD_SCRIPT3 = bootloader_flash3.ld

# # === Toolchain ===
# CC = arm-none-eabi-gcc
# LD = arm-none-eabi-gcc
# AS = arm-none-eabi-as
# OBJCOPY = arm-none-eabi-objcopy
# SIZE = arm-none-eabi-size

# # === Flags ===
# CFLAGS1 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
#          -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_1
# CFLAGS2 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
#          -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_2
# CFLAGS3 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
#          -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_3

# LDFLAGS1 = -T$(LD_SCRIPT1) -nostdlib -Wl,-Map=$(TARGET).map
# LDFLAGS2 = -T$(LD_SCRIPT2) -nostdlib -Wl,-Map=$(TARGET).map
# LDFLAGS3 = -T$(LD_SCRIPT3) -nostdlib -Wl,-Map=$(TARGET).map

# SOURCES = bootloader.c startup.c
# OBJECTS1 = bootloader1.o startup1.o  # $(SOURCES:.c=.o)
# OBJECTS2 = bootloader2.o startup2.o
# OBJECTS3 = bootloader3.o startup3.o

# # === Build Rules ===
# all: $(TARGET1).bin $(TARGET1).hex $(TARGET2).bin $(TARGET2).hex $(TARGET3).bin $(TARGET3).hex

# %1.o: %.c
# 	$(CC) $(CFLAGS1) -c $< -o $@

# %2.o: %.c
# 	$(CC) $(CFLAGS2) -c $< -o $@

# %3.o: %.c
# 	$(CC) $(CFLAGS3) -c $< -o $@

# $(TARGET1).elf: $(OBJECTS1)
# 	$(LD) $(CFLAGS1) $(LDFLAGS1) $(OBJECTS1) -o $@
# 	$(SIZE) $@

# $(TARGET2).elf: $(OBJECTS2)
# 	$(LD) $(CFLAGS2) $(LDFLAGS2) $(OBJECTS2) -o $@
# 	$(SIZE) $@

# $(TARGET3).elf: $(OBJECTS3)
# 	$(LD) $(CFLAGS3) $(LDFLAGS3) $(OBJECTS3) -o $@
# 	$(SIZE) $@

# %.bin: %.elf
# 	$(OBJCOPY) -O binary $< $@

# %.hex: %.elf
# 	$(OBJCOPY) -O ihex $< $@
