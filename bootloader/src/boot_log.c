#include "boot_log.h"

#include <stdarg.h>
#include <stdint.h>

#include "SEGGER_RTT.h" // Control-block layout only; stage 2 does not link SEGGER_RTT.c (slot is 8 KiB).
#include "hal_delay.h"

#define BOOT_LOG_CHANNEL (1u)
#define BOOT_LOG_BUFFER_SIZE (512u)
#define BOOT_LOG_LINE_SIZE (96u)

// Same section (and so the same address) as the OS's control block; see SEGGER_RTT_Conf.h.
SEGGER_RTT_CB _SEGGER_RTT __attribute__((section(SEGGER_RTT_SECTION)));
static char boot_log_buffer[BOOT_LOG_BUFFER_SIZE];

static inline void memory_barrier(void) {
    __asm__ volatile("dmb" ::: "memory");
}

void boot_log_init(void) {
    // Rebuild the control block unconditionally: RAM keeps the previous boot's block across a reset.
    volatile SEGGER_RTT_CB *cb = &_SEGGER_RTT;
    volatile uint8_t *raw = (volatile uint8_t *)cb;
    for (uint32_t i = 0; i < sizeof(_SEGGER_RTT); i++) {
        raw[i] = 0u;
    }
    cb->MaxNumUpBuffers = SEGGER_RTT_MAX_NUM_UP_BUFFERS;
    cb->MaxNumDownBuffers = SEGGER_RTT_MAX_NUM_DOWN_BUFFERS;
    volatile SEGGER_RTT_BUFFER_UP *up = &cb->aUp[BOOT_LOG_CHANNEL];
    up->sName = "Log Output";
    up->pBuffer = boot_log_buffer;
    up->SizeOfBuffer = sizeof(boot_log_buffer);
    up->Flags = SEGGER_RTT_MODE_NO_BLOCK_SKIP;
    memory_barrier();
    // Publish the ID last, back to front, so a host never sees a complete ID on a half-built block (as SEGGER does).
    static const char id[] = "SEGGER RTT";
    for (uint32_t i = sizeof(id) - 1u; i > 0u; i--) {
        cb->acID[i - 1u] = id[i - 1u];
    }
    memory_barrier();
}

static uint32_t pending_bytes(void) {
    volatile SEGGER_RTT_BUFFER_UP *up = &_SEGGER_RTT.aUp[BOOT_LOG_CHANNEL];
    uint32_t write = up->WrOff;
    uint32_t read = up->RdOff;
    return write >= read ? write - read : up->SizeOfBuffer - read + write;
}

// SEGGER_RTT_MODE_NO_BLOCK_SKIP: drop the whole line if it does not fit (no host attached, or host behind).
static void rtt_write(const char *data, uint32_t length) {
    volatile SEGGER_RTT_BUFFER_UP *up = &_SEGGER_RTT.aUp[BOOT_LOG_CHANNEL];
    uint32_t size = up->SizeOfBuffer;
    uint32_t write = up->WrOff;
    if (length >= size - pending_bytes()) {
        return;
    }
    for (uint32_t i = 0; i < length; i++) {
        boot_log_buffer[write] = data[i];
        if (++write == size) {
            write = 0u;
        }
    }
    memory_barrier();
    up->WrOff = write;
}

void boot_log_shutdown(void) {
    // Give an attached host up to ~50 ms to drain the last lines, then invalidate the control block ID.
    for (unsigned int waited = 0; waited < 50u && pending_bytes() != 0u; waited++) {
        delay_ms(1);
    }
    volatile char *id = _SEGGER_RTT.acID;
    for (unsigned int i = 0; i < sizeof(_SEGGER_RTT.acID); i++) {
        id[i] = '\0';
    }
}

static uint32_t append(char *line, uint32_t length, char c) {
    if (length < BOOT_LOG_LINE_SIZE) {
        line[length++] = c;
    }
    return length;
}

static uint32_t append_number(char *line, uint32_t length, uint32_t value, uint32_t base, uint32_t width) {
    char digits[10];
    uint32_t count = 0;
    do {
        uint32_t digit = value % base;
        digits[count++] = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u);
        value /= base;
    } while (value != 0u);
    while (width > count) {
        length = append(line, length, '0');
        width--;
    }
    while (count > 0u) {
        length = append(line, length, digits[--count]);
    }
    return length;
}

void boot_log(const char *format, ...) {
    char line[BOOT_LOG_LINE_SIZE];
    uint32_t length = 0;
    va_list args;
    va_start(args, format);
    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            length = append(line, length, *p);
            continue;
        }
        p++;
        uint32_t width = 0;
        while (*p >= '0' && *p <= '9') { // A leading '0' flag parses as width digit 0.
            width = width * 10u + (uint32_t)(*p++ - '0');
        }
        if (*p == 'u') {
            length = append_number(line, length, va_arg(args, unsigned int), 10u, width);
        } else if (*p == 'x') {
            length = append_number(line, length, va_arg(args, unsigned int), 16u, width);
        } else if (*p == 's') {
            for (const char *s = va_arg(args, const char *); *s != '\0'; s++) {
                length = append(line, length, *s);
            }
        } else if (*p == '%') {
            length = append(line, length, '%');
        } else {
            break; // Unsupported conversion (or a trailing '%'): stop formatting.
        }
    }
    va_end(args);
    rtt_write(line, length);
}
