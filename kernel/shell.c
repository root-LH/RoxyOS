#include "include/shell.h"
#include "include/printk.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/vfs.h"
#include "include/keyboard.h"
#include "include/ata.h"

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_diskread(int argc, char **argv);
static void cmd_diskwrite(int argc, char **argv);
static void cmd_mkfs(int argc, char **argv);

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
    { "rm", cmd_rm },
    { "diskread", cmd_diskread },
    { "diskwrite", cmd_diskwrite },
    { "mkfs", cmd_mkfs }
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
    if (argc < 2)
    {
        printk("Usage: touch <file>\n");
        return;
    }

    if (vfs_create(argv[1]) == 0)
        printk("File created.\n");
    else
        printk("Create failed.\n");
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vfs_list();
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("Usage: cat <file>\n");
        return;
    }

    char buffer[513];

    int size = vfs_read(argv[1], buffer, sizeof(buffer) - 1);

    if (size < 0)
    {
        printk("Read failed.\n");
        return;
    }

    buffer[size] = '\0';

    printk(buffer);
    printk("\n");
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 3)
    {
        printk("Usage: write <file> <text>\n");
        return;
    }

    char buffer[512];
    buffer[0] = '\0';

    for (int i = 2; i < argc; i++)
    {
        strcat(buffer, argv[i]);

        if (i != argc - 1)
            strcat(buffer, " ");
    }

    if (vfs_write(argv[1], buffer, strlen(buffer)) == 0)
        printk("Write complete.\n");
    else
        printk("Write failed.\n");
}

static void cmd_rm(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("Usage: rm <file>\n");
        return;
    }

    if (vfs_delete(argv[1]) == 0)
        printk("File removed.\n");
    else
        printk("Remove failed.\n");
}

static void cmd_diskread(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint8_t sector[512];

    if (ata_read_sector(100, sector) == 0)
    {
        for (int i = 0; i < 32; i++)
        {
            printk_hex(sector[i]);
            printk(" ");
        }

        printk("\n");
    }
    else
    {
        printk("ATA Read Failed\n");
    }
}

static void cmd_diskwrite(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint8_t sector[512];

    memset(sector, 0, sizeof(sector));

    const char *msg = "Hello ATA";

    for (int i = 0; msg[i] != '\0'; i++)
        sector[i] = msg[i];

    if (ata_write_sector(100, sector) == 0)
    {
        printk("Write Success\n");
    }
    else
    {
        printk("Write Failed\n");
    }
}

static void cmd_mkfs(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    simplefs_format();

    printk("SimpleFS formatted.\n");
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