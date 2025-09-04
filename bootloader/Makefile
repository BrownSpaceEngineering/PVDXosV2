# === CONFIG ===
TARGET = bootloader
MCU = cortex-m4
FPU = fpv4-sp-d16
FLOAT_ABI = hard
LD_SCRIPT = bootloader_flash.ld

# === Toolchain ===
CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
AS = arm-none-eabi-as
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

# === Flags ===
CFLAGS = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
         -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g

LDFLAGS = -T$(LD_SCRIPT) -nostdlib -Wl,-Map=$(TARGET).map

SOURCES = bootloader.c startup.c
OBJECTS = $(SOURCES:.c=.o)

# === Build Rules ===
all: $(TARGET).bin $(TARGET).hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@
	$(SIZE) $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.bin *.hex *.map