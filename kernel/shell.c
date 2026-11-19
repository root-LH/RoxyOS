#include "include/shell.h"
#include "include/printk.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/vfs.h"
#include "include/keyboard.h"
#include "include/ata.h"
#include "include/elf_loader.h"
#include "include/console.h"
#include "include/memory.h"
#include "include/process.h"

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
/* 커널(2~1025)과 SimpleFS(2048~) 사이의 빈 섹터 */
#define DISK_SCRATCH_SECTOR 1536

static void cmd_diskread(int argc, char **argv);
static void cmd_diskwrite(int argc, char **argv);
static void cmd_mkfs(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_free(int argc, char **argv);

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
    { "mkfs", cmd_mkfs },
    { "run", cmd_run },
    { "ps", cmd_ps },
    { "free", cmd_free }
};

static const int command_count = sizeof(commands) / sizeof(commands[0]);

void shell_print_prompt(void)
{
    printk("RoxyOS> ");
}

static char input_buffer[INPUT_BUFFER_SIZE];
static uint32_t input_length = 0;
static uint32_t cursor_position = 0;
static uint32_t previous_length = 0;

static void shell_get_screen_pos(uint32_t index, uint16_t *x, uint16_t *y)
{
    uint32_t pos = console_get_input_start_x() + index;

    *x = pos % VGA_WIDTH;
    *y = console_get_input_start_y() + pos / VGA_WIDTH;
}

static void shell_redraw_input(void)
{
    uint16_t x, y;

    for (uint32_t i = 0; i < previous_length; i++)
    {
        shell_get_screen_pos(i, &x, &y);
        vga_putentry_at(' ', x, y);
    }

    for (uint32_t i = 0; i < input_length; i++)
    {
        shell_get_screen_pos(i, &x, &y);
        vga_putentry_at(input_buffer[i], x, y);
    }

    previous_length = input_length;

    shell_get_screen_pos(cursor_position, &x, &y);
    vga_set_cursor(x, y);
}

static const char *shell_readline(void)
{
    input_length = 0;
    cursor_position = 0;
    previous_length = 0;
    input_buffer[0] = '\0';

    console_set_input_start();

    while (1)
    {
        char c = keyboard_getchar();

        if (c == KEY_LEFT)
        {
            if (cursor_position > 0)
            {
                cursor_position--;
                shell_redraw_input();
            }
            continue;
        }

        if (c == KEY_RIGHT)
        {
            if (cursor_position < input_length)
            {
                cursor_position++;
                shell_redraw_input();
            }
            continue;
        }

        if (c == KEY_UP || c == KEY_DOWN)
            continue;

        if (c == '\b')
        {
            if (cursor_position == 0)
                continue;

            for (uint32_t i = cursor_position - 1; i < input_length - 1; i++)
                input_buffer[i] = input_buffer[i + 1];

            input_length--;
            cursor_position--;
            input_buffer[input_length] = '\0';

            shell_redraw_input();
            continue;
        }

        if (c == '\n')
        {
            cursor_position = input_length;
            shell_redraw_input();
            vga_putchar('\n');

            return input_buffer;
        }

        if (input_length >= INPUT_BUFFER_SIZE - 1)
            continue;

        for (uint32_t i = input_length; i > cursor_position; i--)
            input_buffer[i] = input_buffer[i - 1];

        input_buffer[cursor_position] = c;

        input_length++;
        cursor_position++;

        input_buffer[input_length] = '\0';
        shell_redraw_input();
    }
}

void shell_task(void)
{
    while (1)
    {
        shell_print_prompt();

        const char *line = shell_readline();

        shell_execute(line);
    }
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
    printk("  run   - Execute program\n");
    printk("  ps    - List processes\n");
    printk("  free  - Show physical memory usage\n");
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

    int file_size = vfs_size(argv[1]);

    if (file_size < 0)
    {
        printk("Read failed.\n");
        return;
    }

    char *buffer = kmalloc(file_size + 1);

    if (!buffer)
    {
        printk("Out of memory.\n");
        return;
    }

    int size = (file_size == 0) ? 0 : vfs_read(argv[1], buffer, file_size);

    if (size < 0)
    {
        printk("Read failed.\n");
        kfree(buffer);
        return;
    }

    buffer[size] = '\0';

    printk(buffer);
    printk("\n");

    kfree(buffer);
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

    if (ata_read_sector(DISK_SCRATCH_SECTOR, sector) == 0)
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

    if (ata_write_sector(DISK_SCRATCH_SECTOR, sector) == 0)
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

static void cmd_run(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("Usage: run <file>\n");
    }
    else
    {
        process_t *proc = elf_spawn(argv[1]);

        if (!proc)
        {
            kprintf("run: cannot execute %s\n", argv[1]);
            return;
        }

        /* The shell sleeps until the program exits. */
        int code = process_wait(proc);

        if (code != 0)
            kprintf("[%s exited with code %d]\n", argv[1], code);
    }
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
static void cmd_ps(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    process_t *self = process_current();
    int used = 0;

    kprintf("SLOT  PID   TYPE    STATE\n");

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        process_t *proc = process_get_slot(i);

        if (proc->state == PROCESS_UNUSED)
            continue;

        used++;

        kprintf("%-4d  %-4u  %-6s  %s%s\n",
                i, proc->pid,
                proc->type == PROCESS_USER ? "user" : "kernel",
                process_state_name(proc->state),
                proc == self ? " *" : "");
    }

    kprintf("%d/%d slots in use\n", used, MAX_PROCESS);
}

static void cmd_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint32_t total = pmm_total_frame_count();
    uint32_t free = pmm_free_frame_count();

    kprintf("frames: %u total, %u used, %u free (%u KB free)\n",
            total, total - free, free, free * 4);
}
