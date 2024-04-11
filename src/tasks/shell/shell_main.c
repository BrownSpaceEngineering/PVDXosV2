#include "shell_task.h"

struct shellTaskMemory shellMem = {0};

uint8_t SHELL_INPUT_BUFFER[SHELL_INPUT_BUFFER_SIZE] = {0}; //This is a layer ontop of the RTT internal buffer, which is of length 16 (BUFFER_SIZE_DOWN)

//Returns the number of characters read
static size_t get_line_from_terminal(uint8_t* p_linebuffer);
static void clear_RTT_input_buffer();

void shell_main(void *pvParameters) {
    info("Shell task started\n");

    while(1) {
        // Print the shell prompt
        SEGGER_RTT_WriteString(SHELL_RTT_CHANNEL, SHELL_PROMPT);

        //Wait for a command
        size_t cmd_len = get_line_from_terminal(SHELL_INPUT_BUFFER);
        debug("Command received (len %d): %s\n", cmd_len, SHELL_INPUT_BUFFER);

        if (cmd_len == 0) {
            //No command was entered, loop again
            warning("Received empty command in shell task\n");
            continue;
        } else if (cmd_len >= SHELL_INPUT_BUFFER_SIZE) {
            //The command was too long and reached the command limit!
            warning("Command length limit reached! (Max: %d)\n", SHELL_INPUT_BUFFER_SIZE);
            warning("Ignoring command and clearing input buffer\n");
            clear_RTT_input_buffer();
            continue;
        }

        
    }

}

//Returns the number of characters read
static size_t get_line_from_terminal(uint8_t* p_linebuffer){
    //Poll the RTT for input
    size_t linebuffer_idx = 0;
    while(1) {
        int character_read = SEGGER_RTT_GetKey();
        if (character_read < 0 || character_read > 255) {
            //No character was read, nothing's ready yet.
            //Loop again
            continue;
        } else {
            //A character was read, put it into the linebuffer
            p_linebuffer[linebuffer_idx] = (uint8_t)character_read;
            linebuffer_idx++;
            if (linebuffer_idx >= SHELL_INPUT_BUFFER_SIZE) {
                //The linebuffer is full, return 
                return linebuffer_idx;
            }   
        }
    }
    //Make sure the string is null-terminated
    p_linebuffer[linebuffer_idx] = '\0';
    return linebuffer_idx;
}

static void clear_RTT_input_buffer() {
    while(SEGGER_RTT_GetKey() >= 0) {
        //Keep running this loop until there's no more keys to get
    }
}