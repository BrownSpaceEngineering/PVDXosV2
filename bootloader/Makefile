# Compiler and flags
CC = arm-none-eabi-gcc
CFLAGS = -nostdlib -Wall -mcpu=cortex-m4 -mthumb -T linker.ld

# Source files
SRC = bootloader.c

# Object files
OBJ = $(SRC:.c=.o)

# Output file
OUT = out

# Default target
all: $(OUT)

# Rule to create the output file
$(OUT): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

# Rule to compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(OBJ) $(OUT)

# Phony targets
.PHONY: all clean