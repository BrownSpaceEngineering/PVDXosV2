#include "shell_commands.h"
#include "shell_helpers.h"
#include "shell_task.h"

shell_task_memory_t shell_mem = {0};

uint8_t SHELL_INPUT_BUFFER[SHELL_INPUT_BUFFER_SIZE] = {
    0}; // This is a layer ontop of the RTT internal buffer, which is of length 16 (BUFFER_SIZE_DOWN)

void main_shell(void *pvParameters) {
    info("Shell task started\n");
    clear_RTT_input_buffer();
    terminal_printf(RTT_CTRL_TEXT_BRIGHT_YELLOW "\n\n\n\n\n\n\n\nPVDX Shell Initialized! [%s (%s:%s), Built %s]\n" RTT_CTRL_RESET,
                    BUILD_TYPE, GIT_BRANCH_NAME, GIT_COMMIT_HASH, BUILD_DATE " at " BUILD_TIME);

    terminal_printf("%s", SHELL_ASCII_ART);

    while (1) {
        // Print the shell prompt
        terminal_printf(SHELL_PROMPT);

        // Wait for a command
        size_t cmd_len = get_line_from_terminal(SHELL_INPUT_BUFFER);
        debug("Command received (len %d): %s\n", cmd_len, SHELL_INPUT_BUFFER);

        if (cmd_len == 0) {
            // No command was entered, loop again
            debug("Received empty command in shell task\n");
            continue;
        } else if (cmd_len >= SHELL_INPUT_BUFFER_SIZE) {
            // The command was too long and reached the command limit!
            warning("Command length limit reached! (Max: %d)\n", SHELL_INPUT_BUFFER_SIZE);
            warning("Ignoring command and clearing input buffer\n");
            clear_RTT_input_buffer();
            terminal_printf("Command length limit reached! (Max: %d)\n", SHELL_INPUT_BUFFER_SIZE);
            continue;
        }

        char *args[MAX_ARGS]; // Array to hold the command and arguments
        int arg_count = 0;

        // Extract the first word of the command
        char *user_command = strtok((char *)SHELL_INPUT_BUFFER, " ");
        args[arg_count++] = user_command;

        // Continue extracting the rest of the arguments
        while (user_command != NULL && arg_count < MAX_ARGS) {
            args[arg_count] = strtok(NULL, " ");
            if (args[arg_count] == NULL) {
                // Reached the end of the arguments
                break;
            }
            arg_count++;
        }

        // Find the associated command function
        void (*command_func)(char **args, int arg_count) = NULL;
        for (shell_command_t *shell_command = shell_commands; shell_command->command_name != NULL; shell_command++) {
            if (strcmp(user_command, shell_command->command_name) == 0) {
                // It's the right shell command, call the command function
                command_func = shell_command->command_function;
                break;
            }
        }
        if (command_func == NULL) {
            terminal_printf("Command not found: %s\n", user_command);
        } else {
            debug("Running command func for %s\n", user_command);
            command_func(args, arg_count);
            debug("Command func for %s finished\n", user_command);
        }
    }
}