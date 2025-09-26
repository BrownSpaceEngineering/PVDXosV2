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

connect: all
	JLinkExe -CommanderScript flash.jlink \
	&& gdb \
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
