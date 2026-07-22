#include "include/shell.h"
#include "include/printk.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/vfs.h"
#include "include/keyboard.h"

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_rm(int argc, char **argv);

typedef struct
{
    const char *name;
    void (*handler)(int argc, char **argv);
} command_t;

static command_t commands[] =
{
    { "help",  cmd_help  },
    { "clear", cmd_clear },
    { "echo",  cmd_echo  },
    { "version", cmd_version },
    { "touch", cmd_touch },
    { "ls", cmd_ls },
    { "cat", cmd_cat },
    { "write", cmd_write },
    { "rm", cmd_rm }
};

static const int command_count = sizeof(commands) / sizeof(commands[0]);

void shell_init(void)
{
    shell_print_prompt();
}

void shell_print_prompt(void)
{
    printk("RoxyOS> ");
}

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printk("Commands:\n");
    printk("  help  - Show available commands\n");
    printk("  clear - Clear the screen\n");
    printk("  echo  - Print text\n");
    printk("  version  - Show OS version\n");
    printk("  touch - Create a file\n");
    printk("  ls    - List files\n");
    printk("  cat   - Display file contents\n");
    printk("  write - Write text to a file\n");
    printk("  rm    - Remove a file\n");
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_clear();
}

static void cmd_echo(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("\n");
        return;
    }

    for (int i = 1; i < argc; i++)
    {
        printk(argv[i]);

        if (i != argc - 1)
            printk(" ");
    }

    printk("\n");
}

static void cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printk("RoxyOS version 0.1.0\n");
}

static void cmd_touch(int argc, char **argv)
{
    if (argc != 2)
    {
        printk("Usage: touch <filename>\n");
        return;
    }

    if (vfs_create(argv[1]) == NULL)
    {
        printk("Failed to create file\n");
        return;
    }

    printk("Created: ");
    printk(argv[1]);
    printk("\n");
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int found = 0;

    for (size_t i = 0; i < MAX_FILES; i++)
    {
        const file_t *file = vfs_get_file(i);

        if (file == NULL)
            continue;

        printk(file->name);
        printk("\n");

        found = 1;
    }

    if (!found)
        printk("No files\n");
}

static void cmd_cat(int argc, char **argv)
{
    if (argc != 2)
    {
        printk("Usage: cat <filename>\n");
        return;
    }

    file_t *file = vfs_find(argv[1]);

    if (file == NULL)
    {
        printk("File not found\n");
        return;
    }

    const char *data = (const char *)vfs_read(file);

    if (data == NULL)
    {
        printk("(empty)\n");
        return;
    }

    printk(data);
    printk("\n");
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 3)
    {
        printk("Usage: write <filename> <text>\n");
        return;
    }

    file_t *file = vfs_find(argv[1]);

    if (file == NULL)
    {
        printk("File not found\n");
        return;
    }

    char buffer[INPUT_BUFFER_SIZE];
    buffer[0] = '\0';

    for (int i = 2; i < argc; i++)
    {
        strcat(buffer, argv[i]);

        if (i != argc - 1)
            strcat(buffer, " ");
    }

    if (vfs_write(file, buffer, strlen(buffer) + 1) != 0)
    {
        printk("Write failed\n");
        return;
    }

    printk("Write complete\n");
}

static void cmd_rm(int argc, char **argv){
    if (argc != 2)
    {
        printk("Usage: rm <filename>\n");
        return;
    }

    file_t *file = vfs_find(argv[1]);

    if (file == NULL)
    {
        printk("File not found\n");
        return;
    }

    if (vfs_delete(file) != 0)
    {
        printk("Failed to remove file\n");
        return;
    }

    printk("Removed: ");
    printk(argv[1]);
    printk("\n");
}

void shell_execute(const char *cmd)
{
    char buffer[INPUT_BUFFER_SIZE];
    char *argv[16];
    int argc = 0;

    strcpy(buffer, cmd);

    char *token = buffer;

    while (*token && argc < 16)
    {
        while (*token == ' ')
            token++;

        if (*token == '\0')
            break;

        argv[argc++] = token;

        while (*token && *token != ' ')
            token++;

        if (*token == '\0')
            break;

        *token = '\0';
        token++;
    }

    if (argc == 0)
        return;

    for (int i = 0; i < command_count; i++)
    {
        if (strcmp(argv[0], commands[i].name) == 0)
        {
            commands[i].handler(argc, argv);
            return;
        }
    }

    printk("Unknown command\n");
}