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

static void cmd_help(const char *args)
{
    (void)args;

    printk("Commands:\n");
    printk("  help  - Show available commands\n");
    printk("  clear - Clear the screen\n");
    printk("  echo  - Print text\n");
}

static void cmd_clear(const char *args)
{
    (void)args;

    vga_clear();
}

static void cmd_echo(const char *args)
{
    printk(args);
    printk("\n");
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

    while (*args == ' ')
    {
        args++;
    }

    if (strcmp(command, "help") == 0)
    {
        cmd_help(args);
        return;
    }

    if (strcmp(command, "clear") == 0)
    {
        cmd_clear(args);
        return;
    }

    if (strcmp(command, "echo") == 0)
    {
        cmd_echo(args);
        return;
    }

    printk("Unknown command\n");
}