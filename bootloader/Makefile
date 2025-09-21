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
CFLAGS1 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
         -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_1
CFLAGS2 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
         -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_2
CFLAGS3 = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI) \
         -Wall -Werror -O2 -ffreestanding -nostdlib -nostartfiles -g -DBOOTLOADER_3

LDFLAGS1 = -T$(LD_SCRIPT1) -nostdlib -Wl,-Map=$(TARGET).map
LDFLAGS2 = -T$(LD_SCRIPT2) -nostdlib -Wl,-Map=$(TARGET).map
LDFLAGS3 = -T$(LD_SCRIPT3) -nostdlib -Wl,-Map=$(TARGET).map

SOURCES = bootloader.c startup.c
OBJECTS1 = bootloader1.o startup1.o  # $(SOURCES:.c=.o)
OBJECTS2 = bootloader2.o startup2.o
OBJECTS3 = bootloader3.o startup3.o

# === Build Rules ===
all: $(TARGET1).bin $(TARGET1).hex $(TARGET2).bin $(TARGET2).hex $(TARGET3).bin $(TARGET3).hex

%1.o: %.c
	$(CC) $(CFLAGS1) -c $< -o $@

%2.o: %.c
	$(CC) $(CFLAGS2) -c $< -o $@

%3.o: %.c
	$(CC) $(CFLAGS3) -c $< -o $@

$(TARGET1).elf: $(OBJECTS1)
	$(LD) $(CFLAGS1) $(LDFLAGS1) $(OBJECTS1) -o $@
	$(SIZE) $@

$(TARGET2).elf: $(OBJECTS2)
	$(LD) $(CFLAGS2) $(LDFLAGS2) $(OBJECTS2) -o $@
	$(SIZE) $@

$(TARGET3).elf: $(OBJECTS3)
	$(LD) $(CFLAGS3) $(LDFLAGS3) $(OBJECTS3) -o $@
	$(SIZE) $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.bin *.hex *.map