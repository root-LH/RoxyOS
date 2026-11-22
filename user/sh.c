#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/*
 * sh: the RoxyOS user shell.
 *
 *   sh                 interactive (line editing, history, job control)
 *   sh <file> [args]   run a script, one command line per line ('#' comments)
 *   sh -c "<line>"     run one command line
 *
 * Syntax:
 *   'single quotes'    literal text
 *   "double quotes"    text with \" \\ \$ escapes and $? expanded
 *   \c                 c taken literally (outside quotes)
 *   $?  $$             last exit code, the shell's pid
 *   a | b | c          pipeline
 *   < f   > f   >> f   redirect stdin / stdout (truncate, append)
 *   2> f  2>> f  2>&1  redirect stderr
 *   a ; b   a && b   a || b   a &      sequence, and, or, background
 *
 * Builtins: cd pwd exit help history jobs wait
 * Everything else is a program; names without '/' are found in /bin.
 */

#define LINE_MAX      256
#define HISTORY_SIZE  32
#define MAX_TOKENS    64
#define MAX_ARGS      (EXEC_MAX_ARGS - 1)
#define MAX_PIPE      8
#define MAX_JOBS      8
#define WORDS_SIZE    1024

static int interactive = 0;
static int last_status = 0;

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

static void write_all(int fd, const char *buf, int len)
{
    while (len > 0)
    {
        int n = write(fd, buf, (size_t)len);

        if (n <= 0)
            return;

        buf += n;
        len -= n;
    }
}

/* A small output buffer for redrawing the input line in one write. */
static char obuf[1024];
static int olen = 0;

static void oflush(void)
{
    write_all(STDOUT_FILENO, obuf, olen);
    olen = 0;
}

static void oputc(char c)
{
    if (olen == (int)sizeof(obuf))
        oflush();

    obuf[olen++] = c;
}

static void oputs(const char *s)
{
    while (*s)
        oputc(*s++);
}

/* ------------------------------------------------------------------ */
/* Line editor                                                         */
/* ------------------------------------------------------------------ */

static char history[HISTORY_SIZE][LINE_MAX];
static int history_count = 0;

static void history_add(const char *line)
{
    if (line[0] == '\0')
        return;

    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return;

    if (history_count == HISTORY_SIZE)
    {
        memmove(history[0], history[1], sizeof(history[0]) * (HISTORY_SIZE - 1));
        history_count--;
    }

    strcpy(history[history_count++], line);
}

static void print_prompt(void)
{
    char cwd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");

    printf("\033[1;32mRoxyOS\033[0m:\033[1;34m%s\033[0m$ ", cwd);
    fflush(stdout);
}

typedef struct
{
    char buf[LINE_MAX];
    int len;
    int cursor;
    int shown;          /* characters on screen after the last redraw */
} line_t;

/*
 * Redraws the input from the saved cursor position (ESC 7 right after the
 * prompt; the console keeps it in step when the screen scrolls): the text,
 * blanks over what is left of the old text, then the text up to the cursor
 * again so the cursor lands in the right place even across line wraps.
 */
static void redraw(line_t *l)
{
    oputs("\0338");

    for (int i = 0; i < l->len; i++)
        oputc(l->buf[i]);

    for (int i = l->len; i < l->shown; i++)
        oputc(' ');

    oputs("\0338");

    for (int i = 0; i < l->cursor; i++)
        oputc(l->buf[i]);

    oflush();

    l->shown = l->len;
}

static void set_line(line_t *l, const char *text)
{
    strncpy(l->buf, text, LINE_MAX - 1);
    l->buf[LINE_MAX - 1] = '\0';
    l->len = l->cursor = (int)strlen(l->buf);
    redraw(l);
}

/*
 * Reads one line with editing: Left/Right/Home/End, Backspace/Delete,
 * Up/Down history, Ctrl+A/E (start/end), Ctrl+U (erase line), Ctrl+L
 * (clear screen), Ctrl+C (cancel line), Ctrl+D (end of input on an empty
 * line). Returns 0 with the line in out, or -1 at end of input.
 */
static int read_line(char *out)
{
    static line_t l;
    static char draft[LINE_MAX];
    int hpos = history_count;

    l.len = l.cursor = l.shown = 0;
    l.buf[0] = '\0';

    print_prompt();
    oputs("\0337");
    oflush();

    for (;;)
    {
        int c = getkey();

        if (c < 0)
            return -1;

        switch (c)
        {
            case KEY_LEFT:
                if (l.cursor > 0)
                    l.cursor--;
                break;

            case KEY_RIGHT:
                if (l.cursor < l.len)
                    l.cursor++;
                break;

            case KEY_HOME:
            case KEY_CTRL('A'):
                l.cursor = 0;
                break;

            case KEY_END:
            case KEY_CTRL('E'):
                l.cursor = l.len;
                break;

            case KEY_UP:
            case KEY_DOWN:
            {
                int pos = hpos + (c == KEY_UP ? -1 : 1);

                if (pos < 0 || pos > history_count)
                    continue;

                if (hpos == history_count)
                    strcpy(draft, l.buf);

                hpos = pos;
                set_line(&l, pos == history_count ? draft : history[pos]);
                continue;
            }

            case '\b':
                if (l.cursor == 0)
                    continue;

                /* At the end of the line: just erase the last character. */
                if (l.cursor == l.len && l.shown == l.len)
                {
                    l.buf[--l.len] = '\0';
                    l.cursor = l.shown = l.len;
                    oputs("\b \b");
                    oflush();
                    continue;
                }

                /* Then delete the character now under the cursor. */
                l.cursor--;
                /* fall through */

            case KEY_DELETE:
                if (l.cursor >= l.len)
                    continue;

                memmove(l.buf + l.cursor, l.buf + l.cursor + 1,
                        (size_t)(l.len - l.cursor));
                l.len--;
                break;

            case KEY_CTRL('U'):
                set_line(&l, "");
                continue;

            case KEY_CTRL('L'):
                oputs("\033[2J\033[H");
                oflush();
                print_prompt();
                oputs("\0337");
                l.shown = 0;
                break;

            case KEY_CTRL('C'):
                l.cursor = l.len;
                redraw(&l);
                oputs("^C\n");
                oflush();
                out[0] = '\0';
                return 0;

            case KEY_CTRL('D'):
                if (l.len > 0)
                    continue;

                oputs("exit\n");
                oflush();
                return -1;

            case '\n':
                l.cursor = l.len;
                redraw(&l);
                oputc('\n');
                oflush();
                strcpy(out, l.buf);
                history_add(out);
                return 0;

            case '\t':
                c = ' ';
                /* fall through */

            default:
                if (c < ' ' || c >= 0x7F || l.len >= LINE_MAX - 1)
                    continue;

                /* Typing at the end of the line: just print the character. */
                if (l.cursor == l.len && l.shown == l.len)
                {
                    l.buf[l.len++] = (char)c;
                    l.buf[l.len] = '\0';
                    l.cursor = l.shown = l.len;
                    oputc((char)c);
                    oflush();
                    continue;
                }

                memmove(l.buf + l.cursor + 1, l.buf + l.cursor,
                        (size_t)(l.len - l.cursor + 1));
                l.buf[l.cursor++] = (char)c;
                l.len++;
                break;
        }

        l.buf[l.len] = '\0';
        redraw(&l);
    }
}

/* ------------------------------------------------------------------ */
/* Tokenizer                                                           */
/* ------------------------------------------------------------------ */

typedef enum
{
    T_WORD,
    T_PIPE,         /* |  */
    T_AND,          /* && */
    T_OR,           /* || */
    T_AMP,          /* &  */
    T_SEMI,         /* ;  */
    T_IN,           /* <  */
    T_OUT,          /* >  */
    T_APPEND,       /* >> */
    T_ERR,          /* 2> */
    T_ERR_APPEND,   /* 2>> */
    T_ERR_TO_OUT,   /* 2>&1 */
} token_type_t;

typedef struct
{
    token_type_t type;
    char *word;     /* T_WORD */
} token_t;

static token_t tokens[MAX_TOKENS];
static int token_count;

/* Unquoted words are stored here, NUL-separated. */
static char words[WORDS_SIZE];
static int words_used;

static int is_special(char c)
{
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int add_token(token_type_t type, char *word)
{
    if (token_count == MAX_TOKENS)
    {
        fprintf(stderr, "sh: too many words\n");
        return -1;
    }

    tokens[token_count].type = type;
    tokens[token_count].word = word;
    token_count++;

    return 0;
}

static int word_putc(char c)
{
    if (words_used >= WORDS_SIZE - 1)
        return -1;

    words[words_used++] = c;
    return 0;
}

static int word_puts(const char *s)
{
    while (*s)
    {
        if (word_putc(*s++) != 0)
            return -1;
    }

    return 0;
}

/*
 * $? is only known once the commands before it have run ("a; echo $?"),
 * so the tokenizer leaves this marker and expand() fills it in later.
 */
#define STATUS_MARK '\001'

/* $? or $$ at *p: appends it and returns the characters used, or 0. */
static int expand_dollar(const char *p)
{
    char num[16];

    if (p[1] == '?')
        return word_putc(STATUS_MARK) == 0 ? 2 : -1;
    else if (p[1] == '$')
        snprintf(num, sizeof(num), "%d", getpid());
    else
        return 0;

    return word_puts(num) == 0 ? 2 : -1;
}

/* Splits line into tokens. Returns 0, or -1 (message printed). */
static int tokenize(const char *line)
{
    const char *p = line;

    token_count = 0;
    words_used = 0;

    for (;;)
    {
        while (is_space(*p))
            p++;

        if (*p == '\0' || *p == '#')
            return 0;

        /* 2> 2>> 2>&1 (only at the start of a word) */
        if (p[0] == '2' && p[1] == '>')
        {
            if (p[2] == '&' && p[3] == '1')
            {
                if (add_token(T_ERR_TO_OUT, 0) != 0)
                    return -1;
                p += 4;
            }
            else if (p[2] == '>')
            {
                if (add_token(T_ERR_APPEND, 0) != 0)
                    return -1;
                p += 3;
            }
            else
            {
                if (add_token(T_ERR, 0) != 0)
                    return -1;
                p += 2;
            }
            continue;
        }

        if (is_special(*p))
        {
            token_type_t type;

            if (p[0] == '|' && p[1] == '|')      type = T_OR,     p += 2;
            else if (p[0] == '&' && p[1] == '&') type = T_AND,    p += 2;
            else if (p[0] == '>' && p[1] == '>') type = T_APPEND, p += 2;
            else if (p[0] == '|')                type = T_PIPE,   p++;
            else if (p[0] == '&')                type = T_AMP,    p++;
            else if (p[0] == ';')                type = T_SEMI,   p++;
            else if (p[0] == '<')                type = T_IN,     p++;
            else                                 type = T_OUT,    p++;

            if (add_token(type, 0) != 0)
                return -1;
            continue;
        }

        /* A word: runs of plain, quoted and escaped text. */
        char *start = words + words_used;

        while (*p && !is_space(*p) && !is_special(*p))
        {
            int r = 0;

            if (*p == '\'')
            {
                const char *end = strchr(p + 1, '\'');

                if (!end)
                {
                    fprintf(stderr, "sh: missing closing '\n");
                    return -1;
                }

                for (p++; p < end && r == 0; p++)
                    r = word_putc(*p);

                p = end + 1;
            }
            else if (*p == '"')
            {
                for (p++; *p && *p != '"' && r >= 0; )
                {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$'))
                    {
                        r = word_putc(p[1]);
                        p += 2;
                    }
                    else if (*p == '$' && (r = expand_dollar(p)) > 0)
                    {
                        p += r;
                        r = 0;
                    }
                    else
                    {
                        r = word_putc(*p++);
                    }
                }

                if (*p != '"')
                {
                    fprintf(stderr, "sh: missing closing \"\n");
                    return -1;
                }

                p++;
            }
            else if (*p == '\\' && p[1])
            {
                r = word_putc(p[1]);
                p += 2;
            }
            else if (*p == '$' && (r = expand_dollar(p)) > 0)
            {
                p += r;
                r = 0;
            }
            else
            {
                r = word_putc(*p++);
            }

            if (r < 0)
            {
                fprintf(stderr, "sh: line too long\n");
                return -1;
            }
        }

        if (word_putc('\0') != 0 || add_token(T_WORD, start) != 0)
            return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

typedef struct
{
    char *argv[MAX_ARGS + 1];
    int argc;
    char *in;               /* < file */
    char *out;              /* > file */
    int out_append;
    char *err;              /* 2> file */
    int err_append;
    int err_to_out;         /* 2>&1 */
} command_t;

typedef struct
{
    command_t cmds[MAX_PIPE];
    int count;
} pipeline_t;

static int syntax_error(const char *what)
{
    fprintf(stderr, "sh: syntax error: %s\n", what);
    return -1;
}

/*
 * Parses tokens[*pos..] up to a ; && || & (or the end) into p.
 * Returns 0 and leaves *pos at the separator, or -1.
 */
static int parse_pipeline(int *pos, pipeline_t *p)
{
    int i = *pos;

    memset(p, 0, sizeof(*p));
    p->count = 1;

    for (; i < token_count; i++)
    {
        token_t *t = &tokens[i];
        command_t *cmd = &p->cmds[p->count - 1];

        if (t->type == T_AND || t->type == T_OR || t->type == T_AMP || t->type == T_SEMI)
            break;

        switch (t->type)
        {
            case T_WORD:
                if (cmd->argc == MAX_ARGS)
                    return syntax_error("too many arguments");

                cmd->argv[cmd->argc++] = t->word;
                break;

            case T_PIPE:
                if (cmd->argc == 0)
                    return syntax_error("empty command before |");

                if (p->count == MAX_PIPE)
                    return syntax_error("pipeline too long");

                p->count++;
                break;

            case T_ERR_TO_OUT:
                cmd->err_to_out = 1;
                cmd->err = 0;
                break;

            default:        /* redirections take the next word */
                if (i + 1 >= token_count || tokens[i + 1].type != T_WORD)
                    return syntax_error("missing file name after redirection");

                char *file = tokens[++i].word;

                if (t->type == T_IN)
                    cmd->in = file;
                else if (t->type == T_OUT || t->type == T_APPEND)
                    cmd->out = file, cmd->out_append = t->type == T_APPEND;
                else
                    cmd->err = file, cmd->err_append = t->type == T_ERR_APPEND,
                    cmd->err_to_out = 0;
                break;
        }
    }

    if (p->cmds[p->count - 1].argc == 0)
    {
        /* Nothing at all before a separator is an error unless it is the whole line. */
        if (p->count > 1)
            return syntax_error("empty command after |");

        command_t *c = &p->cmds[0];

        if (c->in || c->out || c->err || c->err_to_out)
            return syntax_error("redirection without a command");

        p->count = 0;
    }

    *pos = i;

    return 0;
}

/* Words with $? filled in, for the pipeline about to run. */
static char expanded[WORDS_SIZE];
static int expanded_used;

static char *expand(char *word)
{
    if (!word || !strchr(word, STATUS_MARK))
        return word;

    char num[16];
    char *out = expanded + expanded_used;
    int room = WORDS_SIZE - expanded_used, n = 0;

    snprintf(num, sizeof(num), "%d", last_status);

    for (const char *p = word; *p; p++)
    {
        const char *add = *p == STATUS_MARK ? num : 0;
        int len = add ? (int)strlen(num) : 1;

        if (n + len >= room)
            break;

        memcpy(out + n, add ? add : p, (size_t)len);
        n += len;
    }

    out[n] = '\0';
    expanded_used += n + 1;

    return out;
}

static void expand_pipeline(pipeline_t *p)
{
    expanded_used = 0;

    for (int c = 0; c < p->count; c++)
    {
        command_t *cmd = &p->cmds[c];

        for (int a = 0; a < cmd->argc; a++)
            cmd->argv[a] = expand(cmd->argv[a]);

        cmd->in = expand(cmd->in);
        cmd->out = expand(cmd->out);
        cmd->err = expand(cmd->err);
    }
}

/* ------------------------------------------------------------------ */
/* Redirections                                                        */
/* ------------------------------------------------------------------ */

static int redirect(const char *file, int flags, int target)
{
    int fd = open(file, flags);

    if (fd < 0)
    {
        fprintf(stderr, "sh: %s: cannot open\n", file);
        return -1;
    }

    if (fd != target)
    {
        dup2(fd, target);
        close(fd);
    }

    return 0;
}

/* Applies cmd's redirections to this process's descriptors 0~2. */
static int apply_redirections(const command_t *cmd)
{
    if (cmd->in && redirect(cmd->in, O_RDONLY, STDIN_FILENO) != 0)
        return -1;

    if (cmd->out && redirect(cmd->out, O_WRONLY | O_CREAT |
                             (cmd->out_append ? O_APPEND : O_TRUNC), STDOUT_FILENO) != 0)
        return -1;

    if (cmd->err && redirect(cmd->err, O_WRONLY | O_CREAT |
                             (cmd->err_append ? O_APPEND : O_TRUNC), STDERR_FILENO) != 0)
        return -1;

    if (cmd->err_to_out)
        dup2(STDOUT_FILENO, STDERR_FILENO);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */
/* ------------------------------------------------------------------ */

typedef struct
{
    int used;
    int pgid;
    int pids[MAX_PIPE];
    int count;
    int remaining;
    int status;     /* exit code of the last command */
    char text[48];
} job_t;

static job_t jobs[MAX_JOBS];

static void describe(const pipeline_t *p, char *buf, int size)
{
    buf[0] = '\0';

    for (int c = 0; c < p->count; c++)
    {
        for (int a = 0; a < p->cmds[c].argc; a++)
        {
            int len = (int)strlen(buf);

            snprintf(buf + len, (size_t)(size - len), "%s%s",
                     len ? " " : "", p->cmds[c].argv[a]);
        }

        if (c + 1 < p->count)
        {
            int len = (int)strlen(buf);
            snprintf(buf + len, (size_t)(size - len), " |");
        }
    }
}

/* Records an exited background process. Returns 1 if pid belonged to a job. */
static int job_exited(int pid, int status, int report)
{
    for (int j = 0; j < MAX_JOBS; j++)
    {
        job_t *job = &jobs[j];

        if (!job->used)
            continue;

        for (int k = 0; k < job->count; k++)
        {
            if (job->pids[k] != pid)
                continue;

            if (k == job->count - 1)
                job->status = status;

            if (--job->remaining == 0)
            {
                if (report)
                {
                    if (job->status == 0)
                        printf("[%d] Done          %s\n", j + 1, job->text);
                    else
                        printf("[%d] Exit %-8d %s\n", j + 1, job->status, job->text);
                }

                job->used = 0;
            }

            return 1;
        }
    }

    return 0;
}

/* Collects background jobs that have finished (without blocking). */
static void reap_jobs(void)
{
    int pid, status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        job_exited(pid, status, 1);
}

/* ------------------------------------------------------------------ */
/* Builtins                                                            */
/* ------------------------------------------------------------------ */

static int builtin_cd(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/";

    if (chdir(dir) != 0)
    {
        fprintf(stderr, "cd: %s: no such directory\n", dir);
        return 1;
    }

    return 0;
}

static int builtin_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char cwd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd)))
        return 1;

    printf("%s\n", cwd);
    return 0;
}

static int builtin_exit(int argc, char **argv)
{
    fflush(stdout);
    exit(argc > 1 ? atoi(argv[1]) : last_status);
}

static int builtin_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    for (int i = 0; i < history_count; i++)
        printf("%3d  %s\n", i + 1, history[i]);

    return 0;
}

static int builtin_jobs(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    reap_jobs();

    for (int j = 0; j < MAX_JOBS; j++)
    {
        if (jobs[j].used)
            printf("[%d] Running       %s (pgid %d)\n", j + 1, jobs[j].text, jobs[j].pgid);
    }

    return 0;
}

static int builtin_wait(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int pid, status;

    while ((pid = waitpid(-1, &status, 0)) > 0)
        job_exited(pid, status, 1);

    return 0;
}

static int builtin_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("RoxyOS shell. Builtins: cd [dir]  pwd  exit [code]  history  jobs  wait  help\n"
           "Programs (ls /bin): ls cat echo cp mv rm mkdir rmdir touch stat hexdump\n"
           "  wc grep head edit ps kill free df uptime date lspci clear sleep\n"
           "  reboot shutdown sh ...\n"
           "Syntax: 'quoted' \"quoted $?\" \\c   a | b   < in  > out  >> out  2> err  2>&1\n"
           "        a ; b   a && b   a || b   a &   $? (last exit code)\n"
           "Keys:   Left/Right Home/End Del, Up/Down history, Ctrl+A/E/U/L,\n"
           "        Ctrl+C cancels the line or stops the running job, Ctrl+D exits\n");

    return 0;
}

typedef struct
{
    const char *name;
    int (*fn)(int argc, char **argv);
} builtin_t;

static const builtin_t builtins[] =
{
    { "cd",      builtin_cd      },
    { "pwd",     builtin_pwd     },
    { "exit",    builtin_exit    },
    { "history", builtin_history },
    { "jobs",    builtin_jobs    },
    { "wait",    builtin_wait    },
    { "help",    builtin_help    },
};

static const builtin_t *find_builtin(const char *name)
{
    for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
    {
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    }

    return 0;
}

/* A builtin run by the shell itself: redirections are undone afterwards. */
static int run_builtin_here(const builtin_t *b, command_t *cmd)
{
    int saved[3];

    fflush(stdout);

    for (int fd = 0; fd < 3; fd++)
        saved[fd] = dup(fd);

    int status = 1;

    if (apply_redirections(cmd) == 0)
        status = b->fn(cmd->argc, cmd->argv);

    fflush(stdout);

    for (int fd = 0; fd < 3; fd++)
    {
        if (saved[fd] >= 0)
        {
            dup2(saved[fd], fd);
            close(saved[fd]);
        }
    }

    return status;
}

/* ------------------------------------------------------------------ */
/* Running pipelines                                                   */
/* ------------------------------------------------------------------ */

/* In a forked child: wire up descriptors and become the command. */
static void child_run(command_t *cmd, int in_fd, int out_fd, int close_fd)
{
    if (in_fd >= 0)
    {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    if (out_fd >= 0)
    {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    if (close_fd >= 0)
        close(close_fd);

    if (apply_redirections(cmd) != 0)
        _exit(1);

    const builtin_t *b = find_builtin(cmd->argv[0]);

    if (b)
    {
        int status = b->fn(cmd->argc, cmd->argv);
        exit(status);
    }

    cmd->argv[cmd->argc] = 0;
    exec(cmd->argv[0], cmd->argv);

    fprintf(stderr, "sh: %s: command not found\n", cmd->argv[0]);
    _exit(127);
}

static int run_pipeline(pipeline_t *p, int background)
{
    if (p->count == 0)
        return last_status;

    /* A lone builtin in the foreground changes the shell itself (cd, exit). */
    const builtin_t *b = find_builtin(p->cmds[0].argv[0]);

    if (p->count == 1 && b && !background)
        return run_builtin_here(b, &p->cmds[0]);

    int pids[MAX_PIPE];
    int started = 0;
    int pgid = 0;
    int prev_read = -1;

    fflush(stdout);

    for (int i = 0; i < p->count; i++)
    {
        int fds[2] = { -1, -1 };

        if (i + 1 < p->count && pipe(fds) != 0)
        {
            fprintf(stderr, "sh: cannot create a pipe\n");
            break;
        }

        int pid = fork();

        if (pid < 0)
        {
            fprintf(stderr, "sh: cannot fork (too many processes?)\n");

            if (fds[0] >= 0)
            {
                close(fds[0]);
                close(fds[1]);
            }
            break;
        }

        if (pid == 0)
        {
            /* Join the job's group before exec so Ctrl+C always reaches it. */
            if (interactive)
                setpgid(0, pgid);

            child_run(&p->cmds[i], prev_read, fds[1], fds[0]);
        }

        if (interactive)
        {
            if (pgid == 0)
                pgid = pid;

            setpgid(pid, pgid);
        }

        pids[started++] = pid;

        if (prev_read >= 0)
            close(prev_read);

        if (fds[1] >= 0)
            close(fds[1]);

        prev_read = fds[0];
    }

    if (prev_read >= 0)
        close(prev_read);

    if (started == 0)
        return 1;

    if (background)
    {
        for (int j = 0; j < MAX_JOBS; j++)
        {
            job_t *job = &jobs[j];

            if (job->used)
                continue;

            job->used = 1;
            job->pgid = pgid ? pgid : pids[0];
            job->count = job->remaining = started;
            job->status = 0;
            memcpy(job->pids, pids, sizeof(int) * (size_t)started);
            describe(p, job->text, sizeof(job->text));

            printf("[%d] %d\n", j + 1, job->pgid);
            return 0;
        }

        /* No job slot: the processes still run and are reaped as strays. */
        printf("[?] %d\n", pids[0]);
        return 0;
    }

    if (interactive)
        tcsetpgrp(pgid);

    int status = 0;

    for (int i = 0; i < started; i++)
    {
        int code = 0;

        if (waitpid(pids[i], &code, 0) < 0)
            code = 1;

        if (i == started - 1)
            status = code;
    }

    /* While the shell reads input, Ctrl+C comes to it as a key. */
    if (interactive)
        tcsetpgrp(0);

    return status;
}

/* Runs one command line. Returns the exit code of the last pipeline run. */
static int run_line(const char *line)
{
    if (tokenize(line) != 0)
        return last_status = 2;

    token_type_t connector = T_SEMI;
    int pos = 0;

    while (pos < token_count)
    {
        pipeline_t p;

        if (parse_pipeline(&pos, &p) != 0)
            return last_status = 2;

        token_type_t sep = pos < token_count ? tokens[pos].type : T_SEMI;

        if (pos < token_count)
            pos++;

        if (p.count == 0 && sep != T_SEMI)
        {
            syntax_error("empty command");
            return last_status = 2;
        }

        int run = connector == T_SEMI || connector == T_AMP ||
                  (connector == T_AND && last_status == 0) ||
                  (connector == T_OR && last_status != 0);

        if (run)
        {
            expand_pipeline(&p);
            last_status = run_pipeline(&p, sep == T_AMP);

            /* Ctrl+C (or kill) stopped a foreground job: drop the rest of the line. */
            if (last_status == EXIT_KILLED && sep != T_AMP && p.count > 0)
                break;
        }

        connector = sep;
    }

    return last_status;
}

/* ------------------------------------------------------------------ */
/* Scripts                                                             */
/* ------------------------------------------------------------------ */

static int run_script(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
    {
        fprintf(stderr, "sh: %s: cannot open\n", path);
        return 127;
    }

    char line[LINE_MAX];
    char buf[512];
    int len = 0, n = 0, too_long = 0;

    /* Splits the input into lines; the last one need not end in '\n'. */
    for (int i = 0;; i++)
    {
        if (i == n)
        {
            n = read(fd, buf, sizeof(buf));
            i = 0;
        }

        int end = n <= 0;
        char c = end ? '\n' : buf[i];

        if (c != '\n')
        {
            if (len < LINE_MAX - 1)
                line[len++] = c;
            else
                too_long = 1;
            continue;
        }

        line[len] = '\0';

        if (too_long)
            fprintf(stderr, "sh: line too long, skipped\n");
        else if (len > 0)
            run_line(line);

        len = 0;
        too_long = 0;

        if (end)
            break;
    }

    close(fd);

    return last_status;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "-c") == 0)
        return run_line(argv[2]);

    if (argc > 1)
        return run_script(argv[1]);

    interactive = 1;

    /* The shell leads its own group and takes Ctrl+C as a key while idle. */
    setpgid(0, 0);
    tcsetpgrp(0);

    printf("RoxyOS shell. Type 'help' for commands, 'exit' to leave.\n");

    char line[LINE_MAX];

    for (;;)
    {
        reap_jobs();

        if (read_line(line) != 0)
            break;

        run_line(line);
    }

    return last_status;
}
