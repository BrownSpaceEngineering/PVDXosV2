all: dev

# can't be called bootloader bc that's the folder name
bootloader_target:
	make -C bootloader

dev: bootloader_target
	make -C src dev

release: bootloader_target
	make -C src release

test: bootloader_target
	make -C src test

connect: all
	JLinkExe -CommanderScript flash.jlink \
	&& gdb \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		bootloader/bootloader.elf

clean:
	rm -f PVDXos.bin PVDXos.elf \
	&& make -C bootloader clean \
	&& make -C src clean