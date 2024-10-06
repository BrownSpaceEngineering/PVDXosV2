#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

typedef struct {
    char *command_name;
    void (*command_function)(char **args, int arg_count);
    void (*help_function)();
} ShellCommand;

// Null-terminated array of shell commands
extern ShellCommand shell_commands[];

void shell_help(char **args, int arg_count);
void help_help();

void shell_echo(char **args, int arg_count);
void help_echo();

void shell_clear(char **args, int arg_count);
void help_clear();

void shell_loglevel(char **args, int arg_count);
void help_loglevel();

void shell_reboot(char **args, int arg_count);
void help_reboot();

#endif // SHELL_COMMANDS_H