/**
 * shell_commands.c
 *
 * Implementations of the shell commands available to a user using PVDX's terminal interface.
 *
 * Created: April 11, 2024
 * Author: Oren Kohavi
 */

#include "shell_commands.h"

#include <atmel_start.h>

#include "logging.h"
#include "shell_helpers.h"
#include "watchdog_task.h"

shell_command_t shell_commands[] = {
    {"help", shell_help, help_help},
    {"echo", shell_echo, help_echo},
    {"clear", shell_clear, help_clear},
    {"loglevel", shell_loglevel, help_loglevel},
    {"reboot", shell_reboot, help_reboot},
    {"display", shell_display, help_display},
    {NULL, NULL, NULL} // Null-terminated array
};

/* ---------- HELP COMMAND ---------- */

// This function is a utility that will only exist on the ground station
void shell_help(char **args, int arg_count) {
    if (arg_count == 1) {
        terminal_printf("🚀 Available commands 🚀\n");
        terminal_printf("help - Display this help message\n");
        terminal_printf("echo <message> - Echo the message back to the terminal\n");
        terminal_printf("clear - Clear the terminal screen\n");
        terminal_printf("loglevel <level 0-3> - Set the log level for PVDX terminal output\n");
        terminal_printf("reboot - Reboot the satellite\n");
    } else if (arg_count == 2) {
        for (shell_command_t *shell_command = shell_commands; shell_command->command_name != NULL; shell_command++) {
            if (strcmp(args[1], shell_command->command_name) == 0) {
                shell_command->help_function();
                return;
            }
        }
        terminal_printf("help: Command '%s' not found\n", args[1]);
    } else {
        terminal_printf("help: Invalid usage. Try 'help help'\n");
    }
}

void help_help() {
    terminal_printf("Usage: help\n");
    terminal_printf("\tDisplays the available commands\n");
    terminal_printf("Usage: help <command>\n");
    terminal_printf("\tDisplays the help message for the specified command\n");
}

/* ---------- ECHO COMMAND ---------- */

void shell_echo(char **args, int arg_count) {
    if (arg_count < 2) {
        terminal_printf("Usage: echo <message>\n");
    } else {
        terminal_printf("Echo: '");
        for (int i = 1; i < arg_count; i++) {
            info("Echoing message to terminal: %s\n", args[i]);
            terminal_printf("%s", args[i]);
            if (i < arg_count - 1) {
                terminal_printf(" ");
            }
        }
        terminal_printf("'\n");
    }
}

void help_echo() {
    terminal_printf("Usage: echo <message>\n");
    terminal_printf("\tEchoes the provided message back to the terminal\n");
}

/* ---------- CLEAR COMMAND ---------- */

void shell_clear(char **args, int arg_count) {
    if (arg_count != 1) {
        terminal_printf("Invalid usage. Try 'help clear'\n");
        return;
    }
    terminal_printf(RTT_CTRL_CLEAR RTT_CTRL_RESET); // Clear the terminal screen
}

void help_clear() {
    terminal_printf("Usage: clear\n");
    terminal_printf("\tClears the terminal screen\n");
}

/* ---------- LOGLEVEL COMMAND ---------- */

char *log_level_string_mappings[] = {"DEBUG", "INFO", "EVENT", "WARNING"};

void shell_loglevel(char **args, int arg_count) {
    if (arg_count == 1) {
        terminal_printf("Current log level: %s(%d)\n", log_level_string_mappings[get_log_level()], get_log_level());
    } else if (arg_count == 2) {
        int level = args[1][0]; // Just read the first character
        level = level - '0';    // Convert the character to an integer
        if (level < 0 || level > 3) {
            terminal_printf("Invalid log level. Must be between 0 and 3\n");
        } else {
            log_level_t log_level = (log_level_t)level;
            set_log_level(log_level);
            info("Log level set to %s(%d)\n", log_level_string_mappings[log_level], log_level);
            terminal_printf("Log level set to %s(%d)\n", log_level_string_mappings[log_level], log_level);
        }
    } else {
        terminal_printf("loglevel: Invalid usage. Try 'help loglevel'\n");
    }
}

void help_loglevel() {
    terminal_printf("Usage: loglevel\n");
    terminal_printf("\tDisplays the current log level\n");
    terminal_printf("Usage: loglevel <level>\n");
    terminal_printf("\tSets the log level for PVDX terminal output\n");
    terminal_printf("\t[0] (debug level) ====> Very detailed info about the internals of every process\n");
    terminal_printf("\t[1] (info level)  ====> Every meaningful interaction with the satellite is displayed [DEFAULT]\n");
    terminal_printf("\t[2] (event level) ====> Significant events and interactions displayed\n");
    terminal_printf("\t[3] (warning level) ==> Only errors and critical events are displayed\n");
}

/* ---------- REBOOT COMMAND ---------- */

void shell_reboot(char **args, int arg_count) {
    if (arg_count != 1) {
        terminal_printf("Invalid usage. Try 'help reboot'\n");
        return;
    }
    warning("Reboot command executed by user\n");
    terminal_printf("Rebooting the satellite...\n");
    delay_ms(1000);  // Give the message time to print
    kick_watchdog(); // Kick the watchdog to trigger a reboot
}

void help_reboot() {
    terminal_printf("Usage: reboot\n");
    terminal_printf("\tReboots the satellite\n");
}

/* DISPLAY COMMAND */

void shell_display(char **args, int arg_count) {
    if (arg_count != 2) {
        terminal_printf("Invalid usage. Try 'help display'\n");
        return;
    }

    const uint8_t *image_buffers[] = {IMAGE_BUFFER_BROWNLOGO, IMAGE_BUFFER_PVDX};
    command_t display_image_command = get_display_image_command(image_buffers[args[1][0] - '0']);
    enqueue_command(&display_image_command);
    terminal_printf("fr\n");
}

void help_display() {
    terminal_printf("Usage: display\n");
    terminal_printf("\tgjnerergjkn\n");
}