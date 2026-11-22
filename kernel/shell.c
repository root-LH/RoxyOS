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
#include "include/rtc.h"
#include "include/pci.h"
#include "include/pit.h"
#include "include/acpi.h"

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
static void cmd_date(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_lspci(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_shutdown(int argc, char **argv);
static void cmd_sh(int argc, char **argv);

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
    { "kill", cmd_kill },
    { "date", cmd_date },
    { "uptime", cmd_uptime },
    { "lspci", cmd_lspci },
    { "reboot", cmd_reboot },
    { "shutdown", cmd_shutdown },
    { "sh", cmd_sh }
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

/*
 * Command history: the last SHELL_HISTORY lines, oldest first. Browsing
 * with Up/Down starts past the newest entry, where the line being typed
 * is kept so Down can bring it back.
 */
#define SHELL_HISTORY 16

static char history[SHELL_HISTORY][INPUT_BUFFER_SIZE];
static int history_count = 0;
static int history_pos = 0;
static char history_draft[INPUT_BUFFER_SIZE];

static void history_add(const char *line)
{
    if (line[0] == '\0')
        return;

    /* Repeating the last command does not add another copy. */
    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return;

    if (history_count == SHELL_HISTORY)
    {
        for (int i = 1; i < SHELL_HISTORY; i++)
            strcpy(history[i - 1], history[i]);

        history_count--;
    }

    strcpy(history[history_count++], line);
}

static void shell_set_input(const char *text)
{
    uint32_t n = 0;

    for (; text[n] && n < INPUT_BUFFER_SIZE - 1; n++)
        input_buffer[n] = text[n];

    input_buffer[n] = '\0';

    input_length = n;
    cursor_position = input_length;

    shell_redraw_input();
}

static void history_move(int step)
{
    int pos = history_pos + step;

    if (pos < 0 || pos > history_count)
        return;

    if (history_pos == history_count)
        strcpy(history_draft, input_buffer);

    history_pos = pos;

    shell_set_input(pos == history_count ? history_draft : history[pos]);
}

/* Removes the character at index (0 <= index < input_length). */
static void shell_delete_at(uint32_t index)
{
    for (uint32_t i = index; i < input_length - 1; i++)
        input_buffer[i] = input_buffer[i + 1];

    input_length--;
    input_buffer[input_length] = '\0';
}

static const char *shell_readline(void)
{
    input_length = 0;
    cursor_position = 0;
    previous_length = 0;
    input_buffer[0] = '\0';
    history_pos = history_count;

    console_set_input_start();

    while (1)
    {
        int c = (unsigned char)keyboard_getchar();

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
        {
            history_move(c == KEY_UP ? -1 : 1);
            continue;
        }

        if (c == KEY_HOME || c == KEY_CTRL('A'))
        {
            cursor_position = 0;
            shell_redraw_input();
            continue;
        }

        if (c == KEY_END || c == KEY_CTRL('E'))
        {
            cursor_position = input_length;
            shell_redraw_input();
            continue;
        }

        if (c == KEY_DELETE)
        {
            if (cursor_position < input_length)
            {
                shell_delete_at(cursor_position);
                shell_redraw_input();
            }
            continue;
        }

        if (c == '\b')
        {
            if (cursor_position == 0)
                continue;

            cursor_position--;
            shell_delete_at(cursor_position);

            shell_redraw_input();
            continue;
        }

        /* Ctrl+U: erase the whole line. */
        if (c == KEY_CTRL('U'))
        {
            shell_set_input("");
            continue;
        }

        /* Ctrl+C: give up on this line and start a new one. */
        if (c == KEY_CTRL('C'))
        {
            cursor_position = input_length;
            shell_redraw_input();
            printk("^C\n");

            input_buffer[0] = '\0';
            return input_buffer;
        }

        /* Ctrl+L: clear the screen, keep what was typed. */
        if (c == KEY_CTRL('L'))
        {
            vga_clear();
            shell_print_prompt();
            console_set_input_start();
            previous_length = 0;
            shell_redraw_input();
            continue;
        }

        if (c == '\n')
        {
            cursor_position = input_length;
            shell_redraw_input();
            vga_putchar('\n');

            history_add(input_buffer);

            return input_buffer;
        }

        /* Other control keys and PgUp/PgDn/Insert do nothing here. */
        if ((unsigned char)c < ' ' || (unsigned char)c >= 0x7F)
            continue;

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

/*
 * Runs a program like `run`: the shell sleeps until it exits, with the
 * program's group in the foreground (Ctrl+C). Returns its exit code, or
 * -1 if it could not be started.
 */
static int run_program(int argc, char **argv)
{
    /* The program gets argv[0..]: its own name first, as usual. */
    process_t *proc = elf_spawn(argv[0], argc, argv);

    if (!proc)
        return -1;

    /* A spawned program leads its own process group (pgid == pid). */
    process_set_foreground(proc->pgid);
    int code = process_wait(proc);
    process_set_foreground(0);

    return code;
}

#define USER_SHELL "/bin/sh"

static void start_user_shell(void)
{
    char *argv[] = { USER_SHELL };
    int code = run_program(1, argv);

    if (code < 0)
        kprintf("Cannot start %s; this is the kernel shell.\n", USER_SHELL);
    else
        kprintf("[%s exited with code %d] Kernel shell; 'sh' starts it again.\n",
                USER_SHELL, code);
}

void shell_task(void)
{
    /* The user-mode shell is the normal interface; this one is the fallback. */
    start_user_shell();

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

    printk("Kernel (fallback) shell commands:\n");
    printk("  help clear echo version ps free date uptime lspci [-v] reboot shutdown\n");
    printk("  sh                    - Start the user shell (/bin/sh) again\n");
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
    printk("Keys: Up/Down history, Home/End/Del, Ctrl+A/E/U/L, Ctrl+C stops the\n");
    printk("      running program, Ctrl+Alt+Del reboots\n");
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
        return;
    }

    int code = run_program(argc - 1, argv + 1);

    if (code < 0)
        kprintf("run: cannot execute %s\n", argv[1]);
    else if (code == EXIT_KILLED)
        kprintf("[%s killed]\n", argv[1]);
    else if (code != 0)
        kprintf("[%s exited with code %d]\n", argv[1], code);
}

static void cmd_sh(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    start_user_shell();
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

    kprintf("bcache: %u sectors, %u hits, %u misses, %u writes, %u bulk transfers\n",
            BCACHE_ENTRIES, cache.hits, cache.misses, cache.writes, cache.bulk);
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

static const char *const weekday_names[7] =
{
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static void print_time(const rtc_time_t *t)
{
    kprintf("%u-%02u-%02u %02u:%02u:%02u (%s)",
            t->year, t->month, t->day, t->hour, t->minute, t->second,
            weekday_names[t->weekday % 7]);
}

static void cmd_date(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rtc_time_t now;

    /* Straight from the CMOS clock (not the boot time + ticks estimate). */
    rtc_read(&now);
    print_time(&now);
    kprintf("\n");
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint32_t secs = pit_get_ticks() / PIT_HZ;
    uint32_t days = secs / 86400;
    int procs = 0;

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_get_slot(i)->state != PROCESS_UNUSED)
            procs++;
    }

    rtc_time_t now;
    rtc_from_epoch(rtc_now(), &now);

    kprintf("%02u:%02u:%02u up ", now.hour, now.minute, now.second);

    if (days)
        kprintf("%u day%s, ", days, days == 1 ? "" : "s");

    kprintf("%u:%02u:%02u, %d processes\n",
            secs / 3600 % 24, secs / 60 % 60, secs % 60, procs);

    rtc_time_t boot;
    rtc_from_epoch(rtc_boot_time(), &boot);

    kprintf("booted ");
    print_time(&boot);
    kprintf("\n");
}

static void cmd_lspci(int argc, char **argv)
{
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int count = pci_device_count();

    if (count == 0)
    {
        printk("lspci: no PCI devices\n");
        return;
    }

    for (int i = 0; i < count; i++)
    {
        const pci_device_t *d = pci_get_device(i);
        const char *vendor = pci_vendor_name(d->vendor_id);

        kprintf("%02x:%02x.%u %s: %04x:%04x", d->bus, d->slot, d->func,
                pci_class_name(d), d->vendor_id, d->device_id);

        if (vendor)
            kprintf(" (%s)", vendor);

        kprintf("\n");

        if (!verbose)
            continue;

        kprintf("        class %02x.%02x.%02x rev %02x",
                d->class_code, d->subclass, d->prog_if, d->revision);

        if (d->irq_pin)
            kprintf(", INT%c IRQ %u", 'A' + d->irq_pin - 1, d->irq_line);

        kprintf("\n");

        for (int b = 0; b < 6 && d->header_type == 0; b++)
        {
            uint32_t bar = d->bar[b];

            if (!bar)
                continue;

            if (bar & 1)
                kprintf("        BAR%d: I/O at 0x%x\n", b, bar & ~3u);
            else
                kprintf("        BAR%d: memory at 0x%x\n", b, bar & ~15u);
        }
    }
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printk("Rebooting...\n");
    keyboard_reboot();
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printk(acpi_available() ? "Powering off...\n"
                            : "Powering off (no ACPI, trying emulator ports)...\n");
    acpi_poweroff();
    printk("shutdown: the machine did not power off\n");
}
