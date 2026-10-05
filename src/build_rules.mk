# Extra rules loaded into the ASF child Makefile (via -f) by src/Makefile.
#
# The ASF Makefile's `%.o: %.c` rule can only place an object next to its
# source, so this rule maps $(SRC_DIR)/foo/bar.c -> $(BUILD_DIR)/foo/bar.o
# instead, keeping build artifacts out of src.
#
# Keep the compiler invocation in sync with the `%.o: %.c` rule in ASF/gcc/Makefile.

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@$(MK_DIR) "$(@D)"
	@echo Building file: $<
	@echo ARM/GNU C Compiler
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -x c -mthumb $(CFLAGS) -D__FILENAME__=\"$(notdir $<)\" -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAMD51P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo Finished building: $<
