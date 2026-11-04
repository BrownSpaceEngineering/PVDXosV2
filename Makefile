PVDXOS_DIR = $(CURDIR)

export PVDXOS_DIR

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
			GDBCMD = gdb-multiarch
		else
			# "Pure" Linux
			GDBCMD = gdb-multiarch
		endif
	else
		$(error Unknown or unsupported OS)
	endif
endif

all: dev

# can't be called bootloader bc that's the folder name
bootloader_target:
	make -C bootloader/src

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
	python3 scripts/flip_rand_bit.py --region $(or $(REGION),all)

reset_mcu:
	JLinkExe -CommanderScript reset.jlink

# Flashes flash.bin and asks the bootloader to copy it into MRAM on the next boot, making it the golden copy
flash_to_mram:
	JLinkExe -CommanderScript flash_to_mram.jlink
  
list_files:
	make -C src list_files

# this command will start gdb from a breakpoint at main
# use connect_bl to start from the beginning
connect:
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/src/stage2_1.elf" \
		-ex "add-symbol-file bootloader/src/stage2_2.elf" \
		-ex "add-symbol-file bootloader/src/stage2_3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		-ex "break go_to_app" \
		-ex "continue" \
		-ex "break main" \
		-ex "continue" \
		bootloader/src/stage1.elf

connect_tasks: 
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/src/stage2_1.elf" \
		-ex "add-symbol-file bootloader/src/stage2_2.elf" \
		-ex "add-symbol-file bootloader/src/stage2_3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		-ex "break main" \
		bootloader/src/stage1.elf

# connect for debugging bootloader
connect_bl:
	JLinkExe -CommanderScript flash.jlink \
	&& $(GDBCMD) \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "add-symbol-file bootloader/src/stage2_1.elf" \
		-ex "add-symbol-file bootloader/src/stage2_2.elf" \
		-ex "add-symbol-file bootloader/src/stage2_3.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		bootloader/src/stage1.elf

clean:
	rm -f PVDXos.bin PVDXos.elf \
	&& make -C bootloader/src clean \
	&& make -C src clean
