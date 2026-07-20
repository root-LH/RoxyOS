#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_print_prompt(void);
void shell_execute(const char *cmd);

#endif