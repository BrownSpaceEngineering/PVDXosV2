all:
	make -C bootloader
	make -C src

connect: all
	JLinkExe -CommanderScript flash.jlink
	gdb \
		-ex "set confirm off" \
		-ex "add-symbol-file src/PVDXos.elf" \
		-ex "set confirm on" \
		-ex "target remote localhost:2331" \
		-ex "monitor reset" \
		bootloader/bootloader.elf

clean:
	make -C bootloader clean
	make -C src clean