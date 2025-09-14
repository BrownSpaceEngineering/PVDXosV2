# === CONFIG ===
TARGET1 = bootloader1
TARGET2 = bootloader2
TARGET3 = bootloader3
MCU = cortex-m4
FPU = fpv4-sp-d16
FLOAT_ABI = hard
LD_SCRIPT1 = bootloader_flash1.ld
LD_SCRIPT2 = bootloader_flash2.ld
LD_SCRIPT3 = bootloader_flash3.ld

# === Toolchain ===
CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
AS = arm-none-eabi-as
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

# === Flags ===
CFLAGS = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
         -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g

LDFLAGS1 = -T$(LD_SCRIPT1) -nostdlib -Wl,-Map=$(TARGET).map
LDFLAGS2 = -T$(LD_SCRIPT2) -nostdlib -Wl,-Map=$(TARGET).map
LDFLAGS3 = -T$(LD_SCRIPT3) -nostdlib -Wl,-Map=$(TARGET).map

SOURCES = bootloader.c startup.c
OBJECTS = $(SOURCES:.c=.o)

# === Build Rules ===
all: $(TARGET1).bin $(TARGET1).hex $(TARGET2).bin $(TARGET2).hex $(TARGET3).bin $(TARGET3).hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET1).elf: $(OBJECTS)
	$(LD) $(CFLAGS) $(LDFLAGS1) $(OBJECTS) -o $@
	$(SIZE) $@

$(TARGET2).elf: $(OBJECTS)
	$(LD) $(CFLAGS) $(LDFLAGS2) $(OBJECTS) -o $@
	$(SIZE) $@

$(TARGET3).elf: $(OBJECTS)
	$(LD) $(CFLAGS) $(LDFLAGS3) $(OBJECTS) -o $@
	$(SIZE) $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.bin *.hex *.map