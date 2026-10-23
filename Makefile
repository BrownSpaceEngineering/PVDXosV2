ifeq ($(OS),Windows_NT)
	# Pure Windows (e.g. MSYS2/MinGW). Note that in WSL, $(OS) is *not* Windows_NT.
	GDBCMD = gdb
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
			GDBCMD = gdb
		endif
	else
		$(error Unknown or unsupported OS)
	endif
endif

.PHONY: all bootloader_target dev release test flash_monkey connect connect_bl clean

all: dev

# can't be called bootloader bc that's the folder name
bootloader_target:
	$(MAKE) -C bootloader/src

dev: bootloader_target
	$(MAKE) -C src dev \
	&& python3 scripts/create_flash_segment.py

release: bootloader_target
	$(MAKE) -C src release \
	&& python3 scripts/create_flash_segment.py

test: bootloader_target
	$(MAKE) -C src test \
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
		-ex "add-symbol-file bootloader/src/bootloader2.elf" \
		-ex "add-symbol-file bootloader/src/bootloader3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		-ex "break go_to_app" \
		-ex "continue" \
		-ex "break main" \
		-ex "continue" \
		bootloader/src/bootloader1.elf

# connect for debugging bootloader
connect_bl:
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/src/bootloader2.elf" \
		-ex "add-symbol-file bootloader/src/bootloader3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		bootloader/src/bootloader1.elf

clean:
	rm -f flash.bin \
	&& $(MAKE) -C bootloader/src clean \
	&& $(MAKE) -C src clean
