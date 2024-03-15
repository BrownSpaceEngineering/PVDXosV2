.PHONY: all clean

# Compiler and flags
CC = arm-none-eabi-gcc
AS = arm-none-eabi-as
LD = arm-none-eabi-ld

CFLAGS = -nostartfiles -nostdlib -Wall -Wextra -Wpedantic -Werror -mcpu=cortex-m4 -mthumb -c
ASFLAGS = -mcpu=cortex-m4 -mthumb -c
LDFLAGS = -T linkerscript.ld

TARGET = bootloader.elf

C_SOURCES = bootloader.c
ASM_SOURCES = startup.s

# Object files
OBJECTS=$(C_SOURCES:.c=.o) $(ASM_SOURCES:.s=.o)

# Default rule
all: $(TARGET)

# Rule to link the final target
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

# Rule to compile C source files
%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# Rule to compile assembly source files
%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

# Clean rule
clean:
	rm -f $(OBJECTS) $(TARGET)