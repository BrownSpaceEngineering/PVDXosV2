/**
 * shell_helpers.c
 * 
 * Helper functions for the shell task. This task is responsible for receiving commands from the terminal
 * over RTT and executing them.
 * 
 * Created: April 11, 2024
 * Author: Oren Kohavi
 */

#include "shell_helpers.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Returns the number of characters read
size_t get_line_from_terminal(uint8_t *p_linebuffer) {
    // Poll the RTT for input
    size_t linebuffer_idx = 0;

    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command();

    while (true) {
        // This is really the loop that we expect the program to spend most of its time in, so pet the watchdog here
        enqueue_command(&cmd_checkin);

        int character_read = SEGGER_RTT_GetKey();
        if (character_read < 0 || character_read > 255) {
            // No character was read, nothing's ready yet.
            // Loop again after a delay
            vTaskDelay(pdMS_TO_TICKS(SHELL_INPUT_POLLING_INTERVAL));
            continue;
        } else {
            // A character was read.
            if (character_read == '\r') {
                // Ignore carriage returns
                continue;
            } else if (character_read == '\n') {
                // The user pressed enter, so we're done
                // Don't add the newline to the character buffer
                break;
            }
            // Otherwise, put it into the buffer
            p_linebuffer[linebuffer_idx] = (uint8_t)character_read;
            linebuffer_idx++;
            if (linebuffer_idx >= SHELL_INPUT_BUFFER_SIZE) {
                // The linebuffer is full, return
                break;
            } else {
                // If the line buffer is not full and we haven't reached the newline, keep going
                continue;
            }
        }
    }
    // Make sure the string is null-terminated
    p_linebuffer[linebuffer_idx] = '\0';
    return linebuffer_idx;
}

void clear_RTT_input_buffer() {
    while (SEGGER_RTT_GetKey() >= 0) {
        // Keep running this loop until there's no more keys to get
    }
}

void terminal_printf(const char *string, ...) {
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(SHELL_RTT_CHANNEL, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

bool contains(const char *delim, char c) {
    while (*delim != '\0') {
        if (*delim == c) {
            return true; // Found the character in the delimiter string
        }
        delim++;
    }
    return false; // Character not found
}

char *strtok(char *str, const char *delim) {
    static char *last = NULL;
    if (str == NULL) {
        str = last;
    }
    if (str == NULL || *str == '\0') {
        return NULL;
    }

    // Skip leading delimiters
    while (*str && contains(delim, *str)) {
        str++;
    }

    if (*str == '\0') {
        last = NULL;
        return NULL;
    }

    // Mark the start of the token
    char *start = str;

    // Scan until the end of the token
    while (*str && !contains(delim, *str)) {
        str++;
    }

    if (*str == '\0') {
        last = NULL;
    } else {
        *str = '\0'; // Null-terminate the token
        last = str + 1;
    }

    return start;
}

// Helper function: strchr (since it's typically in the C standard library)
char *strchr(const char *str, int c) {
    while (*str != '\0') {
        if (*str == c) {
            return (char *)str;
        }
        str++;
    }
    return NULL;
}

/* ----- IMPLEMENTATION OF STRCMP TAKEN DIRECTLY FROM GLIBC ----- */
/* https://github.com/lattera/glibc/blob/master/string/ */

int strcmp(const char *p1, const char *p2) {
    const unsigned char *s1 = (const unsigned char *)p1;
    const unsigned char *s2 = (const unsigned char *)p2;
    unsigned char c1, c2;

    do {
        c1 = (unsigned char)*s1++;
        c2 = (unsigned char)*s2++;
        if (c1 == '\0')
            return c1 - c2;
    } while (c1 == c2);

    return c1 - c2;
}