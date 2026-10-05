##############################################################################
################## OBJECT FILE AND INCLUDE PATH DISCOVERY ####################
##############################################################################

# root of the repo - the top-level Makefile exports this, but fall back to the
# parent of this directory so this Makefile can also be invoked directly
# (e.g. `compiledb make` from src/, which can't see through the top-level Makefile)
PVDXOS_DIR ?= $(abspath $(CURDIR)/..)
export PVDXOS_DIR

# list of directories and files to exclude from compilation
EXCLUDE_PATHS := $(PVDXOS_DIR)/src/libraries/adcs \
				 $(PVDXOS_DIR)/src/linalg/Lapack \
				 $(PVDXOS_DIR)/src/linalg/LinearAlgebra/quadprog.c \
				 $(PVDXOS_DIR)/src/linalg/LinearAlgebra/kf.c \
				 $(PVDXOS_DIR)/src/tests/test_linalg.c \
				 $(PVDXOS_DIR)/src/tasks/radio \
				 $(PVDXOS_DIR)/src/drivers/magnetorquer \

# generate flags to pass to find to exclude paths from above
EXCLUDE_FLAGS := $(foreach dir,$(EXCLUDE_PATHS),-not -path "$(dir)/*")

# Root of the source tree, and the build directory that mirrors it
export SRC_DIR := $(PVDXOS_DIR)/src
export BUILD_DIR := $(PVDXOS_DIR)/build

# Discover all c files in src dir
SRCS := $(shell find $(SRC_DIR) -name "*.c" $(EXCLUDE_FLAGS))

# Map src/foo/bar.c -> build/foo/bar.o so artifacts don't clutter src
export OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Create list of directories to include from
export EXTRA_VPATH := $(sort $(dir $(SRCS)))

# Print discovered objs and VPATH for debugging 
$(info Parsed OBJS:)
$(eval $(foreach f,$(OBJS),$(info $(f))))

$(info Parsed EXTRA_VPATH:)
$(eval $(foreach f,$(EXTRA_VPATH),$(info $(f))))

##############################################################################
##############################################################################
##############################################################################
 
#Technical stuff

#Makefile usually uses /bin/sh to evaluate commands, so we need to change it to /bin/bash
#To allow for the if statement in the connect target to execute correctly
SHELL := /bin/bash

#Path to the child makefile
#This makefile is used to build the ASF files, and is provided by Atmel Start
CHILD_MAKEFILE_PATH := ../ASF/gcc

# Invoke the child makefile along with build_rules.mk, which adds the rule for
# compiling src/*.c into BUILD_DIR
CHILD_MAKE := $(MAKE) -C $(CHILD_MAKEFILE_PATH) -f Makefile -f $(SRC_DIR)/build_rules.mk

# Path to the pinconfig submodule and the .atzip it contains
PINCONFIG_DIR := ../PVDX-SAMD-PinConfig
ATZIP_FILE := $(firstword $(wildcard $(PINCONFIG_DIR)/*.atzip))

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
CFLAGS += -Wextra -Werror -Werror=maybe-uninitialized -static
CFLAGS += -Wshadow -Wnull-dereference -Wduplicated-cond -Wlogical-op -Werror=return-type -Wfloat-equal
CFLAGS += -Wdangling-else -Wtautological-compare
CFLAGS += -fwrapv # Enable fwrapv (wrap on overflow of signed integers) just to be safe
CFLAGS += -fsigned-char # Ensure that char is signed as your average c programmer might expect -- it's actually default unsigned on arm!

# Linking to Ccontrol 
# CFLAGS += -L$(PATH_TO_CCONTROL) -lCControl

# Disable warnings for unused parameters due to ASF functions having unused parameters
CFLAGS += -Wno-unused-parameter #Because some ASF functions have unused parameters, supress this warning

# Tests that `make test` can build individually. Each NAME is guarded by #ifdef TEST_NAME in src/tests
# Keep this list in sync with src/tests/test.h
TEST_NAMES := SPP LINALG CFDP USLP INA226

# `make test TESTS="spp cfdp"` builds only those tests by passing -DTEST_SPP -DTEST_CFDP.
# Leaving TESTS empty builds every test
SELECTED_TESTS := $(patsubst TEST_%,%,$(shell echo '$(TESTS)' | tr 'a-z,' 'A-Z '))
UNKNOWN_TESTS := $(filter-out $(TEST_NAMES),$(SELECTED_TESTS))
ifneq ($(UNKNOWN_TESTS),)
    $(error Unknown test(s) in TESTS: $(UNKNOWN_TESTS). Available tests: $(TEST_NAMES))
endif
TEST_DEFINES := $(addprefix -DTEST_,$(SELECTED_TESTS))

# Build-specific flags
CFLAGS_DEV := -DDEVBUILD
CFLAGS_UNITTEST := -DUNITTEST -DDEVBUILD -Wno-unused-variable $(TEST_DEFINES)
CFLAGS_RELEASE := -DRELEASE -O2

### All these variables are exported to the child makefile, and affect its behavior ###
# Build directories the child makefile creates before compiling
export SUB_DIRS := $(patsubst %/,%,$(sort $(dir $(OBJS))))

export DIR_INCLUDES := $(foreach dir,$(EXTRA_VPATH),-I"$(dir)" )

export OBJS_AS_ARGS := $(foreach obj,$(OBJS),$(patsubst ../%,%,$(obj)))

export DEPS_AS_ARGS := $(patsubst %.o,%.d,$(OBJS_AS_ARGS))

.PHONY: all dev release test clean connect update_asf flash_bootloader

# objects files don't depend on CFLAGS, so switching build types (e.g. dev -> test)
# would otherwise relink stale objects, so ecord the last build type and clean on change.
BUILD_TYPE_FILE := $(CHILD_MAKEFILE_PATH)/.build_type

define ensure_build_type
	@if [ "$$(cat $(BUILD_TYPE_FILE) 2>/dev/null)" != "$(1)" ]; then \
		$(MAKE) clean && echo "$(1)" > $(BUILD_TYPE_FILE); \
	fi
endef

# The bootloader copies PVDXos.bin into RAM and runs it from there, so it can't
# be larger than the SAMD51's 256 KB of RAM
MAX_BIN_SIZE := 262144

BUILD_LOG := $(CHILD_MAKEFILE_PATH)/build.log

# Printed when a unit test build doesn't fit in RAM
TEST_SIZE_HINT := echo "Hint: build only the tests you need, e.g. 'make test TESTS=\"spp cfdp\"' (passes -DTEST_SPP -DTEST_CFDP)."; \
	echo "      Available tests: $(TEST_NAMES)";

# $(call size_hint,<build type>) expands to the hint for test builds, and to nothing otherwise
size_hint = $(if $(filter test,$(1)),$(TEST_SIZE_HINT))

# The child make lines below are prefixed with '+' so that they still recurse
# under `make -n`. Make only does that automatically when $(MAKE) appears
# literally in the recipe, and without it dry runs (which compiledb relies on)
# would never see the compile commands.

# $(call child_build,<build type>,<CFLAGS>) runs the child make. The linker
# script's RAM region is smaller than MAX_BIN_SIZE, so an oversized image
# usually fails at link time rather than in check_bin_size; catch that here
# too so test builds still get the hint.
define child_build
	+@set -o pipefail; $(CHILD_MAKE) CFLAGS="$(2)" 2>&1 | tee $(BUILD_LOG) \
	|| { if grep -q "overflowed by\|will not fit in region" $(BUILD_LOG); then \
		echo "ERROR: PVDXos does not fit in RAM (see the linker error above)"; \
		$(call size_hint,$(1)) \
	fi; exit 1; }
endef

# $(call check_bin_size,<build type>) fails the build if PVDXos.bin is bigger than RAM
define check_bin_size
	@size=$$(wc -c < ../ASF/gcc/PVDXos.bin | tr -d ' '); \
	if [ "$$size" -gt $(MAX_BIN_SIZE) ]; then \
		echo "ERROR: PVDXos.bin is $$size bytes, which is larger than RAM ($(MAX_BIN_SIZE) bytes)"; \
		$(call size_hint,$(1)) \
		exit 1; \
	fi
endef

# Default target
all: dev

dev:
	$(call ensure_build_type,dev)
	$(call child_build,dev, $(CFLAGS_DEV) $(CFLAGS))
	$(call check_bin_size,dev)
	@cp -f ../ASF/gcc/PVDXos.bin ./ \
	&& cp -f ../ASF/gcc/PVDXos.elf ./ \
	&& echo " --- Finished Building PVDXos --- " \
	&& echo " ---  THIS IS THE DEV BUILD! --- "

release: clean #Might as well clean before compiling the release build
	@echo "release" > $(BUILD_TYPE_FILE)
	$(call child_build,release, $(CFLAGS_RELEASE) $(CFLAGS))
	$(call check_bin_size,release)
	@cp -f ../ASF/gcc/PVDXos.bin ./ \
	&& cp -f ../ASF/gcc/PVDXos.elf ./ \
	&& echo " ---  Finished Building PVDXos  --- " \
	&& echo " --- THIS IS THE RELEASE BUILD! --- "

# The selected tests are part of the build type, so changing TESTS forces a rebuild
test:
	$(call ensure_build_type,$(strip test $(TEST_DEFINES)))
	$(call child_build,test, $(CFLAGS_UNITTEST) $(CFLAGS))
	$(call check_bin_size,test)
	@cp -f ../ASF/gcc/PVDXos.elf ./ \
	&& cp -f ../ASF/gcc/PVDXos.bin ./ \
	&& echo " ---   Finished Building PVDXos   --- " \
	&& echo " --- THIS IS THE UNIT TEST BUILD! --- "

# Clean target
clean:
	@$(CHILD_MAKE) clean \
	&& rm -rf $(BUILD_DIR) \
	&& rm -f ./PVDXos.bin ./PVDXos.elf $(BUILD_TYPE_FILE) $(BUILD_LOG) \
	&& echo " --- Cleaned Build Files --- "

# this is a cheat, since it's just a nop. Execution stops after 
# info statements at the top of Makefile
list_files:
	@: 

# When updating the ASF configuration, this must be run once in order to automatically integrate the new ASF config
# Hopefully nobody ever needs to touch this, but you can add to it if you want to automatically trigger an action when the ASF is updated
# The worst part of this is step 6, making text modifications to the stock ASF Makefile
# IF YOU MODIFY STEP 6, PLEASE MAKE SURE YOU KNOW WHAT YOU'RE DOING
update_asf:
	@if [ -z "$(ATZIP_FILE)" ]; then \
		echo "No .atzip found in $(PINCONFIG_DIR)! Make sure the PVDX-SAMD-PinConfig submodule is initialized (git submodule update --init)."; \
		echo " --- Operation Aborted --- "; \
		exit 1; \
	fi;
	@read -p "WARNING -- Are you sure you want to REPLACE the ASF with $(ATZIP_FILE)? [Y/N] " confirm; \
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
	&& unzip -q $(ATZIP_FILE) -d ../ASF \
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
	&& $(SED) -i 's/AtmelStart/PVDXos/g' ../ASF/gcc/Makefile \
	&& echo "(6.6) ASF Makefile: Project name updated to PVDXos" \
	&& rm -f ../ASF/main.c \
	&& echo "(7) ASF main.c removed" \
	&& $(SED) -i 's|// <h> Basic|#define configSUPPORT_STATIC_ALLOCATION 1\n#define INCLUDE_xQueueGetMutexHolder 1|' ../ASF/config/FreeRTOSConfig.h \
	&& echo "(8.1) ASF FreeRTOSConfig.h: Static allocation & QueueGetMutexHolder enabled" \
	&& $(SED) -i 's|#define INCLUDE_uxTaskGetStackHighWaterMark 0|#define INCLUDE_uxTaskGetStackHighWaterMark 1|' ../ASF/config/FreeRTOSConfig.h \
	&& echo "(8.2) ASF FreeRTOSConfig.h: Task stack high watermark function enabled" \
	&& $(SED) -i 's|#define configCHECK_FOR_STACK_OVERFLOW 1|#define configCHECK_FOR_STACK_OVERFLOW 2|' ../ASF/config/FreeRTOSConfig.h \
	&& echo "(8.3) ASF FreeRTOSConfig.h: Task stack overflow checking upgraded to type 2 (higher accuracy)" \
	&& $(SED) -i "/#define INCLUDE_xTaskGetCurrentTaskHandle 0/a #endif \n\n// \<q\> Include thread-local storage pointers \n// \<id\> freertos_num_thread_local_storage_pointers \n#ifndef configNUM_THREAD_LOCAL_STORAGE_POINTERS \n#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1" ../ASF/config/FreeRTOSConfig.h \
	&& echo "(8.4) ASF FreeRTOSConfig.h: Thread-local storage enabled" \
	&& $(SED) -i 's|"\.\./samd51a/gcc/gcc/samd51p20a_flash\.ld"|"\.\./\.\./src/src_ram\.ld"|' ../ASF/gcc/Makefile \
	&& echo "(9) ASF Linker Script: ASF Makefile updated to use custom flash script" \
	&& find ../ASF -type f -newermt now -exec touch {} + \
	&& echo "(10) Timestamps in future updated to present" \
	&& echo " --- Finished Integrating ASF --- "
