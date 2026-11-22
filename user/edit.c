#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
 * edit [file]: a full-screen text editor for the 80x25 console.
 *
 *   Arrows, Home/End, PgUp/PgDn   move
 *   Backspace/Delete/Enter/Tab    edit (Tab inserts spaces to the next 4)
 *   Ctrl+S save    Ctrl+Q quit    Ctrl+F find    Ctrl+N find next
 *   Ctrl+G go to line             Ctrl+K cut line    Ctrl+U paste line
 *
 * Draws with ANSI escapes (see kernel/drivers/console.c) and takes the
 * console out of the foreground group so Ctrl+C arrives as a key.
 */

#define SCREEN_COLS 80
#define SCREEN_ROWS 25
#define TEXT_ROWS   (SCREEN_ROWS - 2)   /* then a status bar and a message line */
#define TAB_STOP    8
#define INDENT      4

typedef struct
{
    char *text;
    int len;
    int cap;
} row_t;

static row_t *rows = NULL;
static int row_count = 0;
static int row_cap = 0;

static int cx = 0, cy = 0;          /* cursor: character index, row */
static int row_off = 0, col_off = 0;
static int dirty = 0;
static int quit_warned = 0;

static char filename[PATH_MAX];
static char message[SCREEN_COLS];
static char last_search[64];
static char *cut_line = NULL;

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static char *out = NULL;
static int out_len = 0, out_cap = 0;

static void emit(const char *s, int len)
{
    if (out_len + len > out_cap)
    {
        int cap = out_cap ? out_cap * 2 : 4096;

        while (cap < out_len + len)
            cap *= 2;

        char *p = realloc(out, (size_t)cap);

        if (!p)
            return;

        out = p;
        out_cap = cap;
    }

    memcpy(out + out_len, s, (size_t)len);
    out_len += len;
}

static void emits(const char *s)
{
    emit(s, (int)strlen(s));
}

static void flush_out(void)
{
    for (int done = 0; done < out_len; )
    {
        int n = write(STDOUT_FILENO, out + done, (size_t)(out_len - done));

        if (n <= 0)
            break;

        done += n;
    }

    out_len = 0;
}

static void set_message(const char *fmt, const char *arg)
{
    snprintf(message, sizeof(message), fmt, arg);
}

/* ------------------------------------------------------------------ */
/* Rows                                                                */
/* ------------------------------------------------------------------ */

static void die(const char *why)
{
    const char *reset = "\033[0m\033[2J\033[H\033[?25h";

    write(STDOUT_FILENO, reset, strlen(reset));
    fprintf(stderr, "edit: %s\n", why);
    tcsetpgrp(getpgid(0));
    exit(1);
}

static void row_reserve(row_t *r, int need)
{
    if (need + 1 <= r->cap)
        return;

    int cap = r->cap ? r->cap : 16;

    while (cap < need + 1)
        cap *= 2;

    char *p = realloc(r->text, (size_t)cap);

    if (!p)
        die("out of memory");

    r->text = p;
    r->cap = cap;
}

static void insert_row(int at, const char *text, int len)
{
    if (row_count == row_cap)
    {
        int cap = row_cap ? row_cap * 2 : 64;
        row_t *p = realloc(rows, sizeof(row_t) * (size_t)cap);

        if (!p)
            die("out of memory");

        rows = p;
        row_cap = cap;
    }

    memmove(&rows[at + 1], &rows[at], sizeof(row_t) * (size_t)(row_count - at));
    row_count++;

    row_t *r = &rows[at];

    r->text = NULL;
    r->len = r->cap = 0;
    row_reserve(r, len);
    memcpy(r->text, text, (size_t)len);
    r->text[len] = '\0';
    r->len = len;
}

static void delete_row(int at)
{
    free(rows[at].text);
    memmove(&rows[at], &rows[at + 1], sizeof(row_t) * (size_t)(row_count - at - 1));
    row_count--;
}

static void row_insert(row_t *r, int at, const char *s, int len)
{
    row_reserve(r, r->len + len);
    memmove(r->text + at + len, r->text + at, (size_t)(r->len - at + 1));
    memcpy(r->text + at, s, (size_t)len);
    r->len += len;
}

static void row_delete(row_t *r, int at, int len)
{
    memmove(r->text + at, r->text + at + len, (size_t)(r->len - at - len + 1));
    r->len -= len;
}

/* Screen column of character index i (tabs expand to TAB_STOP). */
static int render_col(const row_t *r, int i)
{
    int col = 0;

    for (int k = 0; k < i && k < r->len; k++)
        col = r->text[k] == '\t' ? (col / TAB_STOP + 1) * TAB_STOP : col + 1;

    return col;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

static int load(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;

    char buf[1024];
    char *line = NULL;
    int len = 0, cap = 0, n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                insert_row(row_count, line ? line : "", len);
                len = 0;
                continue;
            }

            if (buf[i] == '\r')
                continue;

            if (len + 1 >= cap)
            {
                cap = cap ? cap * 2 : 128;
                char *p = realloc(line, (size_t)cap);

                if (!p)
                    die("out of memory");

                line = p;
            }

            line[len++] = buf[i];
        }
    }

    if (len > 0)
        insert_row(row_count, line, len);

    free(line);
    close(fd);

    return n < 0 ? -1 : 0;
}

static int prompt(const char *label, char *buf, int size);

static void save(void)
{
    if (filename[0] == '\0' &&
        (prompt("Save as: ", filename, sizeof(filename)) != 0 || filename[0] == '\0'))
    {
        set_message("Save cancelled", NULL);
        return;
    }

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);

    if (fd < 0)
    {
        set_message("Cannot open %s for writing", filename);
        return;
    }

    long total = 0;
    int ok = 1;

    for (int i = 0; i < row_count && ok; i++)
    {
        row_t *r = &rows[i];

        /* Writes are at most 4 KB per call: loop until the row is out. */
        for (int done = 0; done < r->len && ok; )
        {
            int n = write(fd, r->text + done, (size_t)(r->len - done));

            if (n <= 0)
                ok = 0;
            else
                done += n;
        }

        if (ok && write(fd, "\n", 1) != 1)
            ok = 0;

        total += r->len + 1;
    }

    close(fd);

    if (!ok)
    {
        set_message("Write error on %s (disk full?)", filename);
        return;
    }

    char text[SCREEN_COLS];

    snprintf(text, sizeof(text), "Wrote %ld bytes to %%s", total);
    set_message(text, filename);
    dirty = 0;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void scroll(void)
{
    if (cy < row_off)
        row_off = cy;

    if (cy >= row_off + TEXT_ROWS)
        row_off = cy - TEXT_ROWS + 1;

    int rx = cy < row_count ? render_col(&rows[cy], cx) : 0;

    if (rx < col_off)
        col_off = rx;

    if (rx >= col_off + SCREEN_COLS)
        col_off = rx - SCREEN_COLS + 1;
}

static void draw_row(int y)
{
    char pos[16];

    /* Erase first: a full 80-column row moves the cursor to the next line. */
    snprintf(pos, sizeof(pos), "\033[%d;1H\033[K", y + 1);
    emits(pos);

    int file_row = row_off + y;

    if (file_row >= row_count)
    {
        emits("\033[34m~\033[0m");
        return;
    }

    row_t *r = &rows[file_row];
    char line[SCREEN_COLS];
    int n = 0, col = 0;

    for (int i = 0; i < r->len && col < col_off + SCREEN_COLS; i++)
    {
        int width = r->text[i] == '\t' ? TAB_STOP - col % TAB_STOP : 1;

        for (int k = 0; k < width; k++, col++)
        {
            if (col < col_off || col >= col_off + SCREEN_COLS)
                continue;

            unsigned char c = (unsigned char)r->text[i];

            line[n++] = c == '\t' ? ' ' : (c < ' ' || c >= 0x7F) ? '?' : (char)c;
        }
    }

    emit(line, n);
}

static void draw_status(void)
{
    char left[SCREEN_COLS + 1], right[32], bar[SCREEN_COLS + 1];

    snprintf(left, sizeof(left), " %s%s", filename[0] ? filename : "[new file]",
             dirty ? " [modified]" : "");
    snprintf(right, sizeof(right), "line %d/%d, col %d ", cy + 1,
             row_count ? row_count : 1, cy < row_count ? render_col(&rows[cy], cx) + 1 : 1);

    int l = (int)strlen(left), r = (int)strlen(right);

    if (l + r > SCREEN_COLS)
        l = SCREEN_COLS - r;

    memset(bar, ' ', SCREEN_COLS);
    memcpy(bar, left, (size_t)l);
    memcpy(bar + SCREEN_COLS - r, right, (size_t)r);

    char pos[16];

    snprintf(pos, sizeof(pos), "\033[%d;1H\033[7m", TEXT_ROWS + 1);
    emits(pos);
    emit(bar, SCREEN_COLS);
    emits("\033[0m");

    /* Message line: the last row, so stop one short of the corner (no scroll). */
    snprintf(pos, sizeof(pos), "\033[%d;1H\033[K", SCREEN_ROWS);
    emits(pos);
    emit(message, (int)strlen(message));
}

static void refresh(void)
{
    scroll();

    emits("\033[?25l");

    for (int y = 0; y < TEXT_ROWS; y++)
        draw_row(y);

    draw_status();

    char pos[24];
    int rx = cy < row_count ? render_col(&rows[cy], cx) : 0;

    snprintf(pos, sizeof(pos), "\033[%d;%dH\033[?25h", cy - row_off + 1, rx - col_off + 1);
    emits(pos);

    flush_out();
}

/* ------------------------------------------------------------------ */
/* Input on the message line                                           */
/* ------------------------------------------------------------------ */

/* Reads a line into buf (Enter accepts, Esc/Ctrl+C cancels). 0 = accepted. */
static int prompt(const char *label, char *buf, int size)
{
    int len = (int)strlen(buf);

    for (;;)
    {
        snprintf(message, sizeof(message), "%s%s", label, buf);
        refresh();

        /* Put the cursor after the typed text. */
        char pos[16];
        int col = (int)strlen(message) + 1;

        snprintf(pos, sizeof(pos), "\033[%d;%dH", SCREEN_ROWS, col < SCREEN_COLS ? col : SCREEN_COLS - 1);
        emits(pos);
        flush_out();

        int c = getkey();

        if (c == '\n')
        {
            message[0] = '\0';
            return 0;
        }

        if (c < 0 || c == 27 || c == KEY_CTRL('C') || c == KEY_CTRL('Q'))
        {
            message[0] = '\0';
            return -1;
        }

        if (c == '\b')
        {
            if (len > 0)
                buf[--len] = '\0';
        }
        else if (c >= ' ' && c < 0x7F && len < size - 1 &&
                 (int)strlen(label) + len < SCREEN_COLS - 2)
        {
            buf[len++] = (char)c;
            buf[len] = '\0';
        }
    }
}

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

static void clamp_cursor(void)
{
    if (cy >= row_count)
        cy = row_count ? row_count - 1 : 0;

    int len = cy < row_count ? rows[cy].len : 0;

    if (cx > len)
        cx = len;
}

static void ensure_row(void)
{
    if (row_count == 0)
        insert_row(0, "", 0);
}

static void insert_char(char c)
{
    ensure_row();
    row_insert(&rows[cy], cx++, &c, 1);
    dirty = 1;
}

static void insert_newline(void)
{
    ensure_row();

    row_t *r = &rows[cy];

    insert_row(cy + 1, r->text + cx, r->len - cx);

    r = &rows[cy];              /* insert_row may have moved the array */
    r->len = cx;
    r->text[cx] = '\0';

    cy++;
    cx = 0;
    dirty = 1;
}

static void backspace(void)
{
    if (cy >= row_count || (cx == 0 && cy == 0))
        return;

    if (cx > 0)
    {
        row_delete(&rows[cy], --cx, 1);
    }
    else
    {
        /* Join this line onto the previous one. */
        row_t *prev = &rows[cy - 1];

        cx = prev->len;
        row_insert(prev, prev->len, rows[cy].text, rows[cy].len);
        delete_row(cy);
        cy--;
    }

    dirty = 1;
}

static void delete_char(void)
{
    if (cy >= row_count)
        return;

    if (cx < rows[cy].len)
    {
        row_delete(&rows[cy], cx, 1);
    }
    else if (cy + 1 < row_count)
    {
        row_insert(&rows[cy], cx, rows[cy + 1].text, rows[cy + 1].len);
        delete_row(cy + 1);
    }
    else
    {
        return;
    }

    dirty = 1;
}

static void cut(void)
{
    if (cy >= row_count)
        return;

    free(cut_line);
    cut_line = strdup(rows[cy].text);

    if (row_count == 1)
    {
        rows[0].len = 0;
        rows[0].text[0] = '\0';
    }
    else
    {
        delete_row(cy);
    }

    cx = 0;
    clamp_cursor();
    dirty = 1;
    set_message("Line cut (Ctrl+U pastes it)", NULL);
}

static void paste(void)
{
    if (!cut_line)
        return;

    insert_row(cy < row_count ? cy : row_count, cut_line, (int)strlen(cut_line));
    cx = 0;
    dirty = 1;
}

static void find_next(int from_next)
{
    if (last_search[0] == '\0' || row_count == 0)
        return;

    int len = (int)strlen(last_search);

    /* Search forward from just after the cursor, wrapping around once. */
    for (int k = 0; k <= row_count; k++)
    {
        int y = (cy + k) % row_count;
        row_t *r = &rows[y];
        int start = k == 0 ? cx + from_next : 0;

        if (k == row_count)
            start = 0;      /* back on the first row: its start too */

        for (int x = start; x + len <= r->len; x++)
        {
            if (memcmp(r->text + x, last_search, (size_t)len) == 0)
            {
                cy = y;
                cx = x;
                message[0] = '\0';
                return;
            }
        }
    }

    set_message("Not found: %s", last_search);
}

static void find(void)
{
    char query[sizeof(last_search)];

    strcpy(query, last_search);

    if (prompt("Find: ", query, sizeof(query)) != 0 || query[0] == '\0')
        return;

    strcpy(last_search, query);
    find_next(0);
}

static void goto_line(void)
{
    char buf[12] = "";

    if (prompt("Go to line: ", buf, sizeof(buf)) != 0 || buf[0] == '\0')
        return;

    int line = atoi(buf);

    if (line < 1)
        line = 1;

    cy = line - 1;
    cx = 0;
    clamp_cursor();
}

static void move(int key)
{
    int len = cy < row_count ? rows[cy].len : 0;

    switch (key)
    {
        case KEY_LEFT:
            if (cx > 0)
                cx--;
            else if (cy > 0)
                cx = rows[--cy].len;
            break;

        case KEY_RIGHT:
            if (cx < len)
                cx++;
            else if (cy + 1 < row_count)
                cy++, cx = 0;
            break;

        case KEY_UP:
            if (cy > 0)
                cy--;
            break;

        case KEY_DOWN:
            if (cy + 1 < row_count)
                cy++;
            break;

        case KEY_HOME: cx = 0; break;
        case KEY_END:  cx = len; break;

        case KEY_PGUP:
            cy = cy > TEXT_ROWS ? cy - TEXT_ROWS : 0;
            break;

        case KEY_PGDN:
            cy += TEXT_ROWS;
            break;
    }

    clamp_cursor();
}

/* Returns 0 when the editor should exit. */
static int handle_key(int c)
{
    if (c != KEY_CTRL('Q'))
        quit_warned = 0;

    switch (c)
    {
        case KEY_CTRL('Q'):
            if (dirty && !quit_warned)
            {
                set_message("Unsaved changes: Ctrl+Q again to quit, Ctrl+S to save", NULL);
                quit_warned = 1;
                return 1;
            }
            return 0;

        case KEY_CTRL('S'): save(); break;
        case KEY_CTRL('F'): find(); break;
        case KEY_CTRL('N'): find_next(1); break;
        case KEY_CTRL('G'): goto_line(); break;
        case KEY_CTRL('K'): cut(); break;
        case KEY_CTRL('U'): paste(); break;

        case KEY_CTRL('C'):
            set_message("Ctrl+Q quits, Ctrl+S saves", NULL);
            break;

        case '\n': insert_newline(); break;
        case '\b': backspace(); break;
        case KEY_DELETE: delete_char(); break;

        case '\t':
            do
                insert_char(' ');
            while (render_col(&rows[cy], cx) % INDENT != 0);
            break;

        case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
        case KEY_HOME: case KEY_END: case KEY_PGUP: case KEY_PGDN:
            move(c);
            break;

        default:
            if (c >= ' ' && c < 0x7F)
                insert_char((char)c);
            break;
    }

    return 1;
}

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        if (strlen(argv[1]) >= sizeof(filename))
        {
            fprintf(stderr, "edit: file name too long\n");
            return 1;
        }

        strcpy(filename, argv[1]);

        struct stat st;

        if (stat(filename, &st) == 0 && st.st_mode == S_IFDIR)
        {
            fprintf(stderr, "edit: %s is a directory\n", filename);
            return 1;
        }

        if (load(filename) != 0)
            set_message("New file: %s", filename);
    }

    if (!message[0])
        set_message("Ctrl+S save | Ctrl+Q quit | Ctrl+F find | Ctrl+G go to line", NULL);

    /* Ctrl+C becomes a key while editing; given back to our group on exit. */
    int old_fg = tcgetpgrp();
    tcsetpgrp(0);

    for (;;)
    {
        refresh();

        int c = getkey();

        if (c < 0 || !handle_key(c))
            break;
    }

    emits("\033[0m\033[2J\033[H\033[?25h");
    flush_out();
    tcsetpgrp(old_fg);

    return 0;
}
