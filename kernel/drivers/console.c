#include "../include/console.h"
#include "../include/vga.h"

static uint16_t input_start_x = 0;
static uint16_t input_start_y = 0;

/*
 * A small subset of ANSI/VT100 escape sequences, so user programs (the
 * shell's line editor, the text editor) can draw on the screen:
 *
 *   ESC [ r ; c H   (or f)   cursor to row r, column c (1-based)
 *   ESC [ n A/B/C/D          cursor up/down/right/left n (default 1)
 *   ESC [ n J                erase: 0 to end of screen, 1 to cursor, 2 all
 *   ESC [ n K                erase: 0 to end of line, 1 to cursor, 2 line
 *   ESC [ ... m              colors: 0 reset, 1 bold (bright), 7 reverse,
 *                            30~37/90~97 foreground, 40~47/100~107 background,
 *                            39/49 default
 *   ESC [ s, ESC 7           save cursor;  ESC [ u, ESC 8  restore it
 *   ESC [ ? 25 l / h         hide / show the cursor
 *
 * The saved cursor moves up with the text when the screen scrolls, so a
 * line editor can always return to where its input started. Anything else
 * is swallowed.
 */

#define DEFAULT_ATTR 0x0F       /* white on black, as at boot */
#define MAX_PARAMS   4

enum { ST_NORMAL, ST_ESC, ST_CSI };

static int state = ST_NORMAL;
static int params[MAX_PARAMS];
static int param_count;
static int private_mode;        /* ESC [ ? ... */

static int saved_x = 0, saved_y = 0;
static int bold = 0, reverse = 0;
static uint8_t fg = DEFAULT_ATTR & 0x0F, bg = DEFAULT_ATTR >> 4;

/* ANSI color order (black red green yellow blue magenta cyan white) -> VGA. */
static const uint8_t ansi_to_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static void apply_attr(void)
{
    uint8_t f = fg, b = bg;

    if (bold)
        f |= 0x08;

    if (reverse)
    {
        uint8_t t = f;
        f = b;
        b = t;
    }

    vga_set_attr((uint8_t)(f | (b << 4)));
}

static int param(int i, int def)
{
    return i < param_count && params[i] > 0 ? params[i] : def;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void move_to(int x, int y)
{
    vga_set_cursor((uint16_t)clamp(x, 0, VGA_WIDTH - 1),
                   (uint16_t)clamp(y, 0, VGA_HEIGHT - 1));
}

static void sgr(void)
{
    if (param_count == 0)
        param_count = 1, params[0] = 0;

    for (int i = 0; i < param_count; i++)
    {
        int p = params[i];

        if (p == 0)
        {
            bold = reverse = 0;
            fg = DEFAULT_ATTR & 0x0F;
            bg = DEFAULT_ATTR >> 4;
        }
        else if (p == 1)               bold = 1;
        else if (p == 22)              bold = 0;
        else if (p == 7)               reverse = 1;
        else if (p == 27)              reverse = 0;
        else if (p >= 30 && p <= 37)   fg = ansi_to_vga[p - 30];
        else if (p >= 90 && p <= 97)   fg = ansi_to_vga[p - 90] | 0x08;
        else if (p == 39)              fg = DEFAULT_ATTR & 0x0F;
        else if (p >= 40 && p <= 47)   bg = ansi_to_vga[p - 40];
        else if (p >= 100 && p <= 107) bg = ansi_to_vga[p - 100] | 0x08;
        else if (p == 49)              bg = DEFAULT_ATTR >> 4;
    }

    apply_attr();
}

static void csi_command(char c)
{
    int x = vga_get_cursor_x();
    int y = vga_get_cursor_y();
    uint32_t here = (uint32_t)(y * VGA_WIDTH + x);

    if (private_mode)
    {
        if (param(0, 0) == 25 && (c == 'l' || c == 'h'))
            vga_show_cursor(c == 'h');
        return;
    }

    switch (c)
    {
        case 'H':
        case 'f':
            move_to(param(1, 1) - 1, param(0, 1) - 1);
            break;

        case 'A': move_to(x, y - param(0, 1)); break;
        case 'B': move_to(x, y + param(0, 1)); break;
        case 'C': move_to(x + param(0, 1), y); break;
        case 'D': move_to(x - param(0, 1), y); break;

        case 'J':
            switch (param_count ? params[0] : 0)
            {
                case 0: vga_clear_cells(here, VGA_WIDTH * VGA_HEIGHT); break;
                case 1: vga_clear_cells(0, here + 1); break;
                case 2: vga_clear_cells(0, VGA_WIDTH * VGA_HEIGHT); break;
            }
            break;

        case 'K':
        {
            uint32_t line = (uint32_t)(y * VGA_WIDTH);

            switch (param_count ? params[0] : 0)
            {
                case 0: vga_clear_cells(here, line + VGA_WIDTH); break;
                case 1: vga_clear_cells(line, here + 1); break;
                case 2: vga_clear_cells(line, line + VGA_WIDTH); break;
            }
            break;
        }

        case 'm': sgr(); break;
        case 's': saved_x = x; saved_y = y; break;
        case 'u': move_to(saved_x, saved_y); break;
    }
}

void console_clear(void)
{
    vga_clear();
}

void console_putchar(char c)
{
    switch (state)
    {
        case ST_NORMAL:
            if (c == 27)
            {
                state = ST_ESC;
                return;
            }

            vga_putchar(c);
            return;

        case ST_ESC:
            state = ST_NORMAL;

            if (c == '[')
            {
                state = ST_CSI;
                param_count = 0;
                private_mode = 0;
                params[0] = 0;
            }
            else if (c == '7')
            {
                saved_x = vga_get_cursor_x();
                saved_y = vga_get_cursor_y();
            }
            else if (c == '8')
            {
                move_to(saved_x, saved_y);
            }
            return;

        case ST_CSI:
            if (c >= '0' && c <= '9')
            {
                if (param_count == 0)
                    param_count = 1, params[0] = 0;

                if (params[param_count - 1] < 1000)
                    params[param_count - 1] = params[param_count - 1] * 10 + (c - '0');
                return;
            }

            if (c == ';')
            {
                if (param_count == 0)
                    param_count = 1, params[0] = 0;

                if (param_count < MAX_PARAMS)
                    params[param_count++] = 0;
                return;
            }

            if (c == '?')
            {
                private_mode = 1;
                return;
            }

            state = ST_NORMAL;

            /* Final byte (0x40~0x7E); anything else aborts the sequence. */
            if (c >= 0x40 && c <= 0x7E)
                csi_command(c);
            return;
    }
}

void console_write(const char *str)
{
    while (*str)
        console_putchar(*str++);
}

void console_set_input_start(void)
{
    input_start_x = vga_get_cursor_x();
    input_start_y = vga_get_cursor_y();
}

uint16_t console_get_input_start_x(void)
{
    return input_start_x;
}

uint16_t console_get_input_start_y(void)
{
    return input_start_y;
}

void console_scroll_notify(void)
{
    if (input_start_y > 0)
        input_start_y--;

    if (saved_y > 0)
        saved_y--;
}
