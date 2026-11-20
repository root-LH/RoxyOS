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
#include "include/bcache.h"
#include "include/shell_fs.h"

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_version(int argc, char **argv);
/* 커널(2~1025)과 파일 시스템(2048~) 사이의 빈 섹터 */
#define DISK_SCRATCH_SECTOR 1536

static void cmd_diskread(int argc, char **argv);
static void cmd_diskwrite(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_free(int argc, char **argv);
static void cmd_kill(int argc, char **argv);

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
    { "ls", cmd_ls },
    { "cat", cmd_cat },
    { "touch", cmd_touch },
    { "write", cmd_write },
    { "append", cmd_append },
    { "rm", cmd_rm },
    { "mkdir", cmd_mkdir },
    { "rmdir", cmd_rmdir },
    { "cd", cmd_cd },
    { "pwd", cmd_pwd },
    { "mv", cmd_mv },
    { "cp", cmd_cp },
    { "stat", cmd_stat },
    { "df", cmd_df },
    { "mkfs", cmd_mkfs },
    { "diskread", cmd_diskread },
    { "diskwrite", cmd_diskwrite },
    { "run", cmd_run },
    { "ps", cmd_ps },
    { "free", cmd_free },
    { "kill", cmd_kill }
};

static const int command_count = sizeof(commands) / sizeof(commands[0]);

void shell_print_prompt(void)
{
    kprintf("RoxyOS:%s> ", vfs_getcwd());
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
    printk("  help clear echo version ps free\n");
    printk("  run <prog> [args...]  - Run a program (bare names are searched in /bin)\n");
    printk("  kill <pid>            - Kill a user process\n");
    printk("  ls [dir]  cat <file>  touch <file>  rm <file>  stat <path>\n");
    printk("  write <file> <text>   - Replace a file's contents\n");
    printk("  append <file> <text>  - Add a line to a file\n");
    printk("  mkdir <dir>  rmdir <dir>  cd [dir]  pwd\n");
    printk("  mv <from> <to>  cp <from> <to>\n");
    printk("  df                    - File system usage\n");
    printk("  mkfs                  - Format the disk file system\n");
    printk("  diskread / diskwrite  - Test sector 1536\n");
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

static void cmd_run(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("Usage: run <file> [args...]\n");
    }
    else
    {
        /* The program gets argv[1..]: its own name first, as usual. */
        process_t *proc = elf_spawn(argv[1], argc - 1, argv + 1);

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

    kprintf("SLOT  PID   PPID  TYPE    STATE    NAME\n");

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        process_t *proc = process_get_slot(i);

        if (proc->state == PROCESS_UNUSED)
            continue;

        used++;

        kprintf("%-4d  %-4u  %-4u  %-6s  %-7s  %s%s\n",
                i, proc->pid, proc->parent ? proc->parent->pid : 0,
                proc->type == PROCESS_USER ? "user" : "kernel",
                process_state_name(proc->state),
                proc->name,
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

    heap_stats_t heap;
    heap_get_stats(&heap);

    kprintf("heap:   %u KB mapped, %u B used, %u B free, %u blocks\n",
            heap.mapped / 1024, heap.used, heap.free, heap.blocks);

    bcache_stats_t cache;
    bcache_get_stats(&cache);

    kprintf("bcache: %u sectors, %u hits, %u misses, %u writes\n",
            BCACHE_ENTRIES, cache.hits, cache.misses, cache.writes);
}

static void cmd_kill(int argc, char **argv)
{
    if (argc < 2)
    {
        printk("Usage: kill <pid>\n");
        return;
    }

    uint32_t pid = 0;

    for (const char *p = argv[1]; *p; p++)
    {
        if (*p < '0' || *p > '9')
        {
            printk("kill: pid must be a number\n");
            return;
        }

        pid = pid * 10 + (uint32_t)(*p - '0');
    }

    if (process_signal_kill(pid) != 0)
        kprintf("kill: no user process %u\n", pid);
}
