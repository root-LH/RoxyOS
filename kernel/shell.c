#include "include/shell.h"
#include "include/printk.h"
#include "include/string.h"
#include "include/vga.h"

void shell_init(void)
{
    shell_print_prompt();
}

void shell_print_prompt(void)
{
    printk("RoxyOS> ");
}

void shell_execute(const char *cmd)
{
    char command[32];
    int i = 0;

    while (cmd[i] && cmd[i] != ' ' && i < (int)sizeof(command) - 1)
    {
        command[i] = cmd[i];
        i++;
    }

    command[i] = '\0';

    const char *args = cmd;

    while (*args && *args != ' ')
    {
        args++;
    }

    if (strcmp(command, "help") == 0)
    {
        printk("Commands:\n");
        printk("  help  - Show available commands\n");
        printk("  clear - Clear the screen\n");
        return;
    }

    if (strcmp(command, "clear") == 0)
    {
        vga_clear();
        return;
    }

    printk("Unknown command\n");
}