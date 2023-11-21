################################################################################
# Automatically-generated file. Do not edit!
################################################################################

ifdef SystemRoot
	SHELL = cmd.exe
	MK_DIR = mkdir
else
	ifeq ($(shell uname), Linux)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), CYGWIN)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), MINGW32)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), MINGW64)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), DARWIN)
		MK_DIR = mkdir -p
	endif
endif

# List the subdirectories for creating object files
SUB_DIRS +=  \
 \
hpl/adc \
hpl/ramecc \
samd51a/gcc/gcc \
examples \
hpl/systick \
hpl/oscctrl \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0 \
hpl/osc32kctrl \
hpl/dmac \
hal/src \
samd51a/gcc \
hal/utils/src \
hpl/wdt \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/GCC/ARM_CM4F \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/MemMang \
hpl/mclk \
hpl/cmcc \
hpl/gclk \
hpl/pm \
hpl/sercom \
hpl/core

# List the object files
OBJS +=  \
hal/src/hal_io.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/list.o \
hpl/systick/hpl_systick.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/GCC/ARM_CM4F/port.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/event_groups.o \
hpl/wdt/hpl_wdt.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/croutine.o \
samd51a/gcc/system_samd51.o \
hpl/core/hpl_core_m4.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/stream_buffer.o \
hal/utils/src/utils_syscalls.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/MemMang/heap_1.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/queue.o \
hal/src/hal_spi_m_sync.o \
hal/src/hal_i2c_m_sync.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/timers.o \
hal/src/hal_delay.o \
hpl/pm/hpl_pm.o \
hpl/core/hpl_init.o \
hpl/gclk/hpl_gclk.o \
hal/src/hal_wdt.o \
hal/utils/src/utils_assert.o \
hpl/dmac/hpl_dmac.o \
hpl/oscctrl/hpl_oscctrl.o \
hal/src/hal_usart_sync.o \
hpl/mclk/hpl_mclk.o \
hpl/ramecc/hpl_ramecc.o \
hal/src/hal_init.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/rtos_port.o \
rtos_start.o \
hal/utils/src/utils_list.o \
hpl/osc32kctrl/hpl_osc32kctrl.o \
examples/driver_examples.o \
driver_init.o \
hal/src/hal_adc_sync.o \
hpl/sercom/hpl_sercom.o \
hal/src/hal_gpio.o \
hal/utils/src/utils_event.o \
hal/src/hal_sleep.o \
hal/src/hal_cache.o \
hpl/cmcc/hpl_cmcc.o \
samd51a/gcc/gcc/startup_samd51.o \
atmel_start.o \
hal/src/hal_atomic.o \
thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/tasks.o \
hpl/adc/hpl_adc.o

OBJS_AS_ARGS +=  \
"hal/src/hal_io.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/list.o" \
"hpl/systick/hpl_systick.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/GCC/ARM_CM4F/port.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/event_groups.o" \
"hpl/wdt/hpl_wdt.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/croutine.o" \
"samd51a/gcc/system_samd51.o" \
"hpl/core/hpl_core_m4.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/stream_buffer.o" \
"hal/utils/src/utils_syscalls.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/MemMang/heap_1.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/queue.o" \
"hal/src/hal_spi_m_sync.o" \
"hal/src/hal_i2c_m_sync.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/timers.o" \
"hal/src/hal_delay.o" \
"hpl/pm/hpl_pm.o" \
"hpl/core/hpl_init.o" \
"hpl/gclk/hpl_gclk.o" \
"hal/src/hal_wdt.o" \
"hal/utils/src/utils_assert.o" \
"hpl/dmac/hpl_dmac.o" \
"hpl/oscctrl/hpl_oscctrl.o" \
"hal/src/hal_usart_sync.o" \
"hpl/mclk/hpl_mclk.o" \
"hpl/ramecc/hpl_ramecc.o" \
"hal/src/hal_init.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/rtos_port.o" \
"rtos_start.o" \
"hal/utils/src/utils_list.o" \
"hpl/osc32kctrl/hpl_osc32kctrl.o" \
"examples/driver_examples.o" \
"driver_init.o" \
"hal/src/hal_adc_sync.o" \
"hpl/sercom/hpl_sercom.o" \
"hal/src/hal_gpio.o" \
"hal/utils/src/utils_event.o" \
"hal/src/hal_sleep.o" \
"hal/src/hal_cache.o" \
"hpl/cmcc/hpl_cmcc.o" \
"samd51a/gcc/gcc/startup_samd51.o" \
"atmel_start.o" \
"hal/src/hal_atomic.o" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/tasks.o" \
"hpl/adc/hpl_adc.o"

# List the directories containing header files
DIR_INCLUDES +=  \
-I"../" \
-I"../config" \
-I"../examples" \
-I"../hal/include" \
-I"../hal/utils/include" \
-I"../hpl/adc" \
-I"../hpl/cmcc" \
-I"../hpl/core" \
-I"../hpl/dmac" \
-I"../hpl/gclk" \
-I"../hpl/mclk" \
-I"../hpl/osc32kctrl" \
-I"../hpl/oscctrl" \
-I"../hpl/pm" \
-I"../hpl/port" \
-I"../hpl/ramecc" \
-I"../hpl/sercom" \
-I"../hpl/systick" \
-I"../hpl/wdt" \
-I"../hri" \
-I"../" \
-I"../config" \
-I"../thirdparty/RTOS" \
-I"../thirdparty/RTOS/freertos/FreeRTOSV10.0.0" \
-I"../thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/include" \
-I"../thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/GCC/ARM_CM4F" \
-I"../thirdparty/RTOS/freertos/FreeRTOSV10.0.0/module_config" \
-I"../" \
-I"../CMSIS/Core/Include" \
-I"../samd51a/include"

# List the dependency files
DEPS := $(OBJS:%.o=%.d)

DEPS_AS_ARGS +=  \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/event_groups.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/croutine.d" \
"hpl/wdt/hpl_wdt.d" \
"hal/utils/src/utils_event.d" \
"hal/src/hal_io.d" \
"hpl/ramecc/hpl_ramecc.d" \
"hpl/systick/hpl_systick.d" \
"hpl/core/hpl_core_m4.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/MemMang/heap_1.d" \
"hal/utils/src/utils_syscalls.d" \
"samd51a/gcc/system_samd51.d" \
"hal/src/hal_i2c_m_sync.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/timers.d" \
"hal/src/hal_spi_m_sync.d" \
"hal/src/hal_wdt.d" \
"hpl/cmcc/hpl_cmcc.d" \
"hpl/dmac/hpl_dmac.d" \
"hal/utils/src/utils_assert.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/stream_buffer.d" \
"hal/src/hal_delay.d" \
"hpl/core/hpl_init.d" \
"hpl/pm/hpl_pm.d" \
"hpl/gclk/hpl_gclk.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/rtos_port.d" \
"hal/src/hal_init.d" \
"hal/src/hal_usart_sync.d" \
"hpl/mclk/hpl_mclk.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/portable/GCC/ARM_CM4F/port.d" \
"driver_init.d" \
"hal/src/hal_adc_sync.d" \
"hpl/osc32kctrl/hpl_osc32kctrl.d" \
"rtos_start.d" \
"examples/driver_examples.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/list.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/queue.d" \
"hal/src/hal_cache.d" \
"hal/utils/src/utils_list.d" \
"hal/src/hal_sleep.d" \
"hpl/sercom/hpl_sercom.d" \
"hal/src/hal_gpio.d" \
"hal/src/hal_atomic.d" \
"hpl/oscctrl/hpl_oscctrl.d" \
"hpl/adc/hpl_adc.d" \
"thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/tasks.d" \
"samd51a/gcc/gcc/startup_samd51.d" \
"atmel_start.d"

OUTPUT_FILE_NAME :=PVDXos
QUOTE := "
OUTPUT_FILE_PATH +=$(OUTPUT_FILE_NAME).elf
OUTPUT_FILE_PATH_AS_ARGS +=$(OUTPUT_FILE_NAME).elf

vpath %.c ../
vpath %.s ../
vpath %.S ../

# All Target
all: $(SUB_DIRS) $(OUTPUT_FILE_PATH)

# Linker target

$(OUTPUT_FILE_PATH): $(OBJS)
	@echo Building target: $(strip $(patsubst ../%, %, $@))
	@echo Invoking: ARM/GNU Linker
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -o $(OUTPUT_FILE_NAME).elf $(OBJS_AS_ARGS) -Wl,--start-group -lm -Wl,--end-group -mthumb \
-Wl,-Map="$(OUTPUT_FILE_NAME).map" --specs=nano.specs -Wl,--gc-sections -mcpu=cortex-m4 \
 \
-T"../samd51a/gcc/gcc/samd51p20a_flash.ld" \
-L"../samd51a/gcc/gcc"
	@echo Finished building target: $(strip $(patsubst ../%, %, $@))

	"arm-none-eabi-objcopy" -O binary "$(OUTPUT_FILE_NAME).elf" "$(OUTPUT_FILE_NAME).bin"
	"arm-none-eabi-objcopy" -O ihex -R .eeprom -R .fuse -R .lock -R .signature  \
        "$(OUTPUT_FILE_NAME).elf" "$(OUTPUT_FILE_NAME).hex"
	"arm-none-eabi-objcopy" -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma \
        .eeprom=0 --no-change-warnings -O binary "$(OUTPUT_FILE_NAME).elf" \
        "$(OUTPUT_FILE_NAME).eep" || exit 0
	"arm-none-eabi-objdump" -h -S "$(OUTPUT_FILE_NAME).elf" > "$(OUTPUT_FILE_NAME).lss"
	"arm-none-eabi-size" "$(OUTPUT_FILE_NAME).elf"

	

# Compiler targets




%.o: %.c
	@echo Building file: $<
	@echo ARM/GNU C Compiler
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAMD51P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.o))"  -o "$(strip $(patsubst ../%, %, $@))" "$<"
	@echo Finished building: $<

%.o: %.s
	@echo Building file: $<
	@echo ARM/GNU Assembler
	$(QUOTE)arm-none-eabi-as$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAMD51P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.o))"  -o "$(strip $(patsubst ../%, %, $@))" "$<"
	@echo Finished building: $<

%.o: %.S
	@echo Building file: $<
	@echo ARM/GNU Preprocessing Assembler
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAMD51P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.d))" -MT"$(patsubst ../%,%, $(@:%.o=%.o))"  -o "$(strip $(patsubst ../%, %, $@))" "$<"
	@echo Finished building: $<

# Detect changes in the dependent files and recompile the respective object files.
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(SUB_DIRS):
	$(MK_DIR) "$(strip $(patsubst ../%, %, $@))"

clean:
	rm -f $(OBJS_AS_ARGS)
	rm -f $(OUTPUT_FILE_PATH)
	rm -f $(DEPS_AS_ARGS)
	rm -f $(OUTPUT_FILE_NAME).a $(OUTPUT_FILE_NAME).hex $(OUTPUT_FILE_NAME).bin \
        $(OUTPUT_FILE_NAME).lss $(OUTPUT_FILE_NAME).eep $(OUTPUT_FILE_NAME).map \
        $(OUTPUT_FILE_NAME).srec