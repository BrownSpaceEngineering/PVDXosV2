ifeq ($(OS),Windows_NT)
	# Pure Windows (e.g. MSYS2/MinGW). Note that in WSL, $(OS) is *not* Windows_NT.
	GDBCMD = gdb-multiarch
else
	# We are in a Unix-like environment (Linux or macOS or WSL). Here, $(OS) is not defined.
	UNAME_S := $(shell uname -s)
	UNAME_R := $(shell uname -r)

	ifeq ($(UNAME_S),Darwin)
		# macOS
		GDBCMD = gdb
	else ifeq ($(UNAME_S),Linux)
		# Linux or WSL. Check if it's WSL by searching "microsoft" in uname -r
		ifneq (,$(findstring microsoft,$(UNAME_R)))
			# WSL
			GDBCMD = gdb
		else
			# "Pure" Linux
			GDBCMD = gdb-multiarch
		endif
	else
		$(error Unknown or unsupported OS)
	endif
endif

# sed selection (GNU sed needed for -i and \n in replacements)
SED ?= sed
ifeq ($(UNAME_S),Darwin)
  SED = gsed
endif

all: dev

# can't be called bootloader bc that's the folder name
bootloader_target:
	make -C bootloader

dev: bootloader_target
	make -C src dev \
	&& python3 scripts/create_flash_segment.py

release: bootloader_target
	make -C src release \
	&& python3 scripts/create_flash_segment.py

test: bootloader_target
	make -C src test \
	&& python3 scripts/create_flash_segment.py

flash_monkey:
	python3 scripts/flip_rand_bit.py

# this command will start gdb from a breakpoint at main
# use connect_bl to start from the beginning
connect:
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/bootloader2.elf" \
		-ex "add-symbol-file bootloader/bootloader3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		-ex "break go_to_app" \
		-ex "continue" \
		-ex "break main" \
		-ex "continue" \
		bootloader/bootloader1.elf

# connect for debugging bootloader
connect_bl:
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/bootloader2.elf" \
		-ex "add-symbol-file bootloader/bootloader3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		bootloader/bootloader1.elf

clean:
	rm -f PVDXos.bin PVDXos.elf \
	&& make -C bootloader clean \
	&& make -C src clean

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