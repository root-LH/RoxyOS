#include "include/shell.h"
#include "include/printk.h"
#include "include/string.h"

void shell_init(void)
{
    shell_print_prompt();
}

void shell_print_prompt(void)
{
    printk("myOS> ");
}

void shell_execute(const char *cmd)
{
    (void)cmd;
}