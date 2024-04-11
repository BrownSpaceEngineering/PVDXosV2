#include "shell_helpers.h"
#include "shell_task.h"

struct shellTaskMemory shellMem = {0};

uint8_t SHELL_INPUT_BUFFER[SHELL_INPUT_BUFFER_SIZE] = {0}; //This is a layer ontop of the RTT internal buffer, which is of length 16 (BUFFER_SIZE_DOWN)

// Returns the number of characters read

void shell_main(void *pvParameters) {
    info("Shell task started\n");

    while(1) {
        // Print the shell prompt
        terminal_vprintf(SHELL_PROMPT);

        //Wait for a command
        size_t cmd_len = get_line_from_terminal(SHELL_INPUT_BUFFER);
        debug("Command received (len %d): %s\n", cmd_len, SHELL_INPUT_BUFFER);

        if (cmd_len == 0) {
            //No command was entered, loop again
            debug("Received empty command in shell task\n");
            continue;
        } else if (cmd_len >= SHELL_INPUT_BUFFER_SIZE) {
            //The command was too long and reached the command limit!
            warning("Command length limit reached! (Max: %d)\n", SHELL_INPUT_BUFFER_SIZE);
            warning("Ignoring command and clearing input buffer\n");
            clear_RTT_input_buffer();
            terminal_vprintf("Command length limit reached! (Max: %d)\n", SHELL_INPUT_BUFFER_SIZE);
            continue;
        }

        char *args[MAX_ARGS]; // Array to hold the command and arguments
        int arg_count = 0;

        // Extract the first word of the command
        char *cmd = strtok((char *)SHELL_INPUT_BUFFER, " ");
        args[arg_count++] = cmd;

        // Continue extracting the rest of the arguments
        while (cmd != NULL && arg_count < MAX_ARGS) {
            args[arg_count] = strtok(NULL, " ");
            if (args[arg_count] == NULL) {
                // Reached the end of the arguments
                break;
            }
            arg_count++;
        }

        if (strcmp(cmd, "help") == 0) {
            terminal_vprintf("Available commands:\n");
            terminal_vprintf("help - Display this help message\n");
            terminal_vprintf("echo <message> - Echo the message back to the terminal\n");
            terminal_vprintf("clear - Clear the terminal\n");
        } else if (strcmp(cmd, "echo") == 0) {
            if (arg_count != 2) {
                terminal_vprintf("Usage: echo <message>\n");
            } else {
                terminal_vprintf("Echo: '%s'\n", args[1]);
            }
        } else if (strcmp(cmd, "clear") == 0) {
            terminal_vprintf(RTT_CTRL_CLEAR);
        } else {
            terminal_vprintf("Unknown command: '%s'\n", cmd);
        }
    }
}