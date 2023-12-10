###############################################################################
##################### ADDING SOMETHING?  READ THIS FIRST! #####################
###############################################################################
###
###  When adding a new C file ot the source, you must do 2 things:
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
../src/tasks/heartbeat_main.o \
../src/misc/printf/SEGGER_RTT.o \
../src/misc/printf/SEGGER_RTT_printf.o \



### ALL DIRECTORIES WITH SOURCE FILES MUST BE LISTED HERE ###
### THESE ARE WRITTEN RELATIVE TO THE ./ASF/gcc/Makefile FILE ###
export EXTRA_VPATH := \
../../src \
../../src/tasks \
../../src/misc \
../../src/misc/printf \


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

# Compiler flags
CFLAGS_POSITIVE := -Wall -Wextra -Werror -Wshadow
CFLAGS_NEGATIVE += -Wno-unused-parameter #Because some ASF functions have unused parameters, supress this warning
CFLAGS := $(CFLAGS_POSITIVE) $(CFLAGS_NEGATIVE)


### All these variables are exported to the child makefile, and affect its behavior ###
export SUB_DIRS := $(shell for dir in $(EXTRA_VPATH); do echo $$dir | $(SED) 's|\.\./||g'; done)

export DIR_INCLUDES := $(CFLAGS) #Slightly hacky way to inject cflags into the child makefile
DIR_INCLUDES += $(foreach dir,$(EXTRA_VPATH),-I"$(dir)" )

export OBJS_AS_ARGS := $(foreach obj,$(OBJS),$(patsubst ../%,%,$(obj)))

export DEPS_AS_ARGS := $(patsubst %.o,%.d,$(OBJS_AS_ARGS))

# Print out DIR_INCLUDES for debugging
#$(info OBJS_AS_ARGS: $(OBJS_AS_ARGS))

# Default target
all:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) \
	&& cp -f ./ASF/gcc/PVDXos.elf ./ \
	&& echo " --- Finished Building PVDXos.elf --- "

# Clean target
clean:
	@$(MAKE) -C $(CHILD_MAKEFILE_PATH) clean \
	&& rm -f ./PVDXos.elf \
	&& echo " --- Cleaned Build Files --- "

# Connects to remote target, loads program, and sets breakpoint at main
connect:
ifeq (,$(findstring microsoft,$(shell uname -r))) #Detects a WSL kernel name, and runs a WSL-specific command for connecting to the GDB server
	@gdb -ex "target remote localhost:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "b main" -ex "continue" ./PVDXos.elf
else #Run the windows-specific command
	@hostname=$(shell hostname) && \
	gdb-multiarch -ex "target remote $$hostname.local:2331" -ex "load" -ex "monitor halt" -ex "monitor reset" -ex "b main" -ex "continue" ./PVDXos.elf
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
	&& echo "(1) ASF Dir Removed" \
	&& rm -f ./atmel_start_config.atstart \
	&& echo "(2) ASF Config Removed" \
	&& mkdir -p ASF \
	&& echo "(3) ASF Dir Re-Created" \
	&& echo "(4.1) Unzipping ASF.atzip (this may take a sec...)" \
	&& unzip -q ASF.atzip -d ASF \
	&& echo "(4.2) ASF Unzipped" \
	&& cp -f ./ASF/atmel_start_config.atstart ./ \
	&& echo "(5) ASF Config Lifted" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.d)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.d))/g' ./ASF/gcc/Makefile \
	&& echo "(6.1) ASF Makefile: GCC dependency filepaths corrected" \
	&& $(SED) -i 's/\$$(\@:%\.o=%\.o)/$$(patsubst ..\/%,%, \$$(\@:%\.o=%\.o))/g' ./ASF/gcc/Makefile \
	&& echo "(6.2) ASF Makefile: GCC object filepaths corrected" \
	&& $(SED) -i 's/\$$@/\$$(strip \$$(patsubst ..\/%, %, $$@))/g' ./ASF/gcc/Makefile \
	&& echo "(6.3) ASF Makefile: GCC output filepaths corrected" \
	&& $(SED) -i '/main/d' ./ASF/gcc/Makefile \
	&& echo "(6.4) ASF Makefile: References to ASF main.c removed" \
	&& rm -f ./ASF/main.c \
	&& echo "(7) ASF main.c Removed" \
	&& $(SED) -i 's/AtmelStart/PVDXos/g' ./ASF/gcc/Makefile \
	&& echo "(8) ASF Makefile: Project name updated to PVDXos" \
	&& echo " --- Finished Integrating ASF --- "



