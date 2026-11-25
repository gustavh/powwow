/*
 *  beam.c  --  code to beam texts across the TCP connection following a
 *              special protocol
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/telnet.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "beam.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"
#include "charset.h"
#include "cmd.h"
#include "regexlib.h"

editsess *edit_sess;            /* head of session list */

char edit_start[BUFSIZE];       /* messages to send to host when starting */
char edit_end[BUFSIZE];         /* or leaving editing sessions */

static void write_message(const char *s)
{
    clear_input_line(opt_compact);
    if (!opt_compact) {
        tty_putc('\n');
        status(1);
    }
    tty_puts(s);
}

/*
 * identify for remote editing; announce UTF-8 support unless the
 * connection uses ISO 8859-1
 */
void mpi_identify(connsess *conn)
{
    static const char latin1[] = MPI "I\n";
    static const char utf8[] = MPI "I1\n1";

    if (conn->charset == CS_LATIN1) {
        tcp_raw_write(conn, latin1, sizeof latin1 - 1);
        conn->ideditor_utf8 = false;
    } else {
        tcp_raw_write(conn, utf8, sizeof utf8 - 1);
        conn->ideditor_utf8 = true;
    }
    conn->ideditor = true;
}

/* the text of an MPI message as it arrives */
struct mpi_text {
    char *text;                 /* the first 'keep' chars of it */
    int size;                   /* allocated for 'text' */
    int kept;                   /* chars in 'text' */
    int keep;                   /* chars to keep, up to #setvar editmem */
    int total;                  /* chars of the message received */
    bool got_iac;               /* the last char was an IAC */
};

/*
 * add char c, received in an MPI message, to its text; an IAC IAC is one
 * IAC; only doing trivial (IAC IAC) processing; this should all be
 * rewritten
 */
static void mpi_text_add(struct mpi_text *t, char c)
{
    if (t->got_iac) {
        t->got_iac = false;
        if (c == (char)IAC)
            return;
        wrap_printf("#warning: unescaped IAC in MPI message, kept as is");
    }
    t->got_iac = c == (char)IAC;
    ++t->total;
    if (t->kept == t->keep)
        return;
    /* the text grows as it arrives, whatever length the header claims */
    if (t->kept == t->size) {
        t->size = t->size > t->keep / 2 ? t->keep : 2 * t->size;
        t->text = xrealloc(t->text, t->size);
    }
    t->text[t->kept++] = c;
}

/* tell the MUD of conn that powwow cancels the editing session key */
static void send_cancel(connsess *conn, unsigned key)
{
    char keystr[16];
    sprintf(keystr, "C%u\n", key);
    char hdr[65];
    sprintf(hdr, "%sE%zu\n%s", MPI, strlen(keystr), keystr);
    tcp_raw_write(conn, hdr, strlen(hdr));
}

/*
 * Process editing protocol message from buf with len remaining chars.
 * Return number of characters used in the message.
 */
int process_message(connsess *conn, char *buf, int len)
{
    /* the input line, and the prompt, drawn again after the message */
    clear_input_line(true);

    /* the length must be positive and fit in an int */
    int msglen = 0;
    bool bad_len = false;
    int i = 1;
    for (; i < len && isdigit((unsigned char)buf[i]); ++i) {
        int digit = buf[i] - '0';
        if (msglen > (INT_MAX - digit) / 10)
            bad_len = true;
        else
            msglen = msglen * 10 + digit;
    }

    if (i < len && buf[i] == '\r')
        ++i;
    if (i >= len || buf[i] != '\n' || bad_len || msglen == 0) {
        write_message("#warning: bad MPI message header\n");
        return 0;
    }
    ++i;

    /* all of a longer message is read, so as to stay in step with the
     * MUD, but only this much of it is kept */
    struct mpi_text t = {
        .keep = (limit_editmem != 0 && msglen > limit_editmem
                 ? limit_editmem : msglen)
    };
    t.size = len - i > BUFSIZE ? len - i : BUFSIZE;
    if (t.size > t.keep)
        t.size = t.keep;
    t.text = xmalloc(t.size);

    char *from = buf + i;
    while (from < buf + len && t.total < msglen)
        mpi_text_add(&t, *from++);
    int used = from - buf;

    bool progress = false;      /* "got N chars out of M" shown */
    while (t.total < msglen) {
        /*
         * read() might block here, but it shouldn't be long; a TLS record
         * without data, such as a key update, fails with EAGAIN; it reads
         * no more than the rest of the message, as each char of it takes
         * at least one
         */
        char chunk[BUFSIZE];
        int want = msglen - t.total;
        if (want > (int)sizeof chunk)
            want = sizeof chunk;
        int l;
        while ((l = conn->fns->read(conn, chunk, want)) < 0
               && (errno == EINTR || errno == EAGAIN
                   || errno == EWOULDBLOCK))
            ;
        if (l <= 0) {
            if (progress)
                tty_putc('\n');
            /* Cygwin reports the end of a #spawn command as a reset or
             * aborted connection */
            if (l == 0 || errno == ECONNRESET || errno == ECONNABORTED)
                wrap_printf("#connection \"%s\" closed in the middle of"
                            " an MPI message", conn->id);
            else
                errmsg("cannot read from connection \"%s\"", conn->id);
            free(t.text);
            return used > len ? len : used;
        }
        for (int n = 0; n < l; ++n)
            mpi_text_add(&t, chunk[n]);
        tty_printf("\r%s#got %d chars out of %d%s", msgattrbeg, t.total,
                   msglen, msgattrend);
        tty_flush();
        progress = true;
    }
    /* as a message, over the progress */
    tty_printf("\r%s", tty_clreoln);
    tty_message("#read all %d chars.\n", msglen);

    char *text = t.text;
    if (t.kept < msglen) {
        if (*buf == 'V') {
            wrap_printf("#warning: text to view cut short to the %d chars"
                        " of #setvar editmem", t.kept);
            msglen = t.kept;
        } else if (*buf == 'E') {
            /* an edited text cut short would replace all of it */
            wrap_printf("#warning: text to edit longer than the %d chars"
                        " of #setvar editmem, not edited", t.kept);
            if (t.kept > 1 && text[0] == 'M'
                && isdigit((unsigned char)text[1]))
                send_cancel(conn, strtoul(text + 1, NULL, 10));
            free(text);
            return used;
        }
    }

    switch (*buf) {
    case 'E':
        message_edit(conn, text, msglen, false, false);
        break;
    case 'V':
        message_edit(conn, text, msglen, true, false);
        break;
    default: {
        char msg[BUFSIZE];
        sprintf(msg, "#warning: unknown MPI message type 0x%02hhx\n", *buf);
        write_message(msg);
        free(text);
        break;
    }
    }

    return used;
}

/*
 * abort an editing session when
 * the MUD socket it comes from gets closed
 */
void abort_edit_fd(connsess *conn)
{
    if (conn == NULL)
        return;

    for (editsess *p = edit_sess; p != NULL; p = p->next) {
        if (p->conn != conn)
            continue;

        /* nothing can be sent to the closed connection */
        p->conn = NULL;
        if (p->cancel)
            continue;           /* already killed, such as by #cancel */
        if (kill(p->pid, SIGKILL) < 0) {        /* Editicide */
            errmsg("cannot stop the editor");
            continue;
        }

        wrap_printf("#aborted \"%s\" (%u)", p->descr, p->key);
        p->cancel = true;
    }
}

/*
 * cancel an editing session; does not free anything (the child death
 * signal handler tells the server and removes the session from the list)
 */
void cancel_edit(editsess *sp)
{
    if (kill(sp->pid, SIGKILL) < 0) {           /* Editicide */
        errmsg("cannot stop the editor");
        return;
    }
    wrap_printf("#killed \"%s\" (%u)", sp->descr, sp->key);
    sp->cancel = true;
}

static ssize_t read_file(int fd, void *buf, size_t count)
{
    char *p = buf;
    size_t result = 0;
    while (count > 0) {
        int r;
        do {
            r = read(fd, p, count);
        } while (r < 0 && errno == EINTR);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        result += r;
        p += r;
        count -= r;
    }
    return result;
}

/*
 * remove what editors may add to the 'len' bytes of the edited text at
 * 'text' with Windows settings: a UTF-8 byte order mark at its start, and
 * the CR of each CR LF; return its new length
 */
static int strip_windows_format(char *text, int len)
{
    int from = 0;
    if (len >= (int)UTF8_BOM_LEN && memcmp(text, UTF8_BOM, UTF8_BOM_LEN) == 0)
        from = UTF8_BOM_LEN;
    int to = 0;
    for (int i = from; i < len; ++i) {
        if (text[i] != '\r' || i + 1 == len || text[i + 1] != '\n')
            text[to++] = text[i];
    }
    return to;
}

/*
 * send back edited text to server, or cancel the editing session if the
 * file was not changed.
 */
static void finish_edit(editsess *sp)
{
    char *realtext = NULL;

    if (sp->conn == NULL)
        goto cleanup_file;

    int fd = open(sp->file, O_RDONLY);
    if (fd == -1) {
        errmsg("cannot open %s", sp->file);
        goto cleanup_file;
    }
    struct stat sbuf;
    if (fstat(fd, &sbuf) == -1) {
        errmsg("cannot read %s", sp->file);
        goto cleanup_fd;
    }

    bool changed = (!sp->cancel
                    && (sbuf.st_mtime > sp->ctime
                        || sbuf.st_size != sp->oldsize));
    /* +1 is for a possible LF, +64 for the header */
    if (changed
        && (sbuf.st_size > INT_MAX - 65
            || (limit_editmem != 0 && sbuf.st_size > limit_editmem))) {
        wrap_printf("#edited text too long, not sent");
        changed = false;
    }

    if (changed) {
        /* file was changed by editor: send back result to server */
        realtext = xmalloc(sbuf.st_size + 65);
        char *text = realtext + 64;
        int txtlen = read_file(fd, text, sbuf.st_size);
        if (txtlen < 0)
            goto cleanup_text;
        if (windows_mode)
            txtlen = strip_windows_format(text, txtlen);

        if (txtlen != 0 && text[txtlen - 1] != '\n') {
            /* If the last line isn't LF-terminated, add an LF;
             * however, empty files must remain empty */
            text[txtlen] = '\n';
            ++txtlen;
        }

        char keystr[16];
        sprintf(keystr, "E%u\n", sp->key);

        char hdr[65];
        sprintf(hdr, "%sE%zu\n%s", MPI, txtlen + strlen(keystr), keystr);

        int hdrlen = strlen(hdr);
        text -= hdrlen;
        memcpy(text, hdr, hdrlen);

        /* text[hdrlen + txtlen] = '\0'; */
        tcp_write_escape_iac(sp->conn, text, hdrlen + txtlen);

        write_message("#completed session ");
        tty_puts(sp->descr);
        tty_printf(" (%u)\n", sp->key);
    } else {
        /* file wasn't changed, cancel editing session */
        send_cancel(sp->conn, sp->key);

        write_message("#cancelled session ");
        tty_puts(sp->descr);
        tty_printf(" (%u)\n", sp->key);
    }

 cleanup_text:
    free(realtext);
 cleanup_fd:
    close(fd);
 cleanup_file:
    if (unlink(sp->file) < 0)
        errmsg("cannot delete %s", sp->file);
}

/* the environment variable 'name', or NULL if it is not set or is empty */
static const char *env_command(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && *value != '\0' ? value : NULL;
}

/*
 * the editor, or the pager if 'view', that Windows mode uses when nothing
 * else is chosen, as for the installer for Windows: nano, only viewing
 * for the pager, in an ssh session, which has no desktop to show Notepad
 * on, else Notepad, in another window so that powwow goes on; NULL
 * without Windows mode or if neither is found
 */
static const char *windows_editor(bool view)
{
    if (!windows_mode)
        return NULL;
    if (getenv("SSH_CONNECTION") != NULL && in_path("nano"))
        return view ? "nano -v" : "nano";
    return in_path("notepad.exe") ? "&notepad.exe" : NULL;
}

/*
 * the command that edits a text, or shows it if 'view', or shows #help if
 * 'builtin': #help first uses #option help-pager, then $POWWOWHELPPAGER;
 * then #option pager and editor come first, then the environment, then
 * the default, which windows_editor() gives in Windows mode; "-" is no
 * pager. If 'from' is not NULL, set it to where the command comes from:
 * NULL for the option asked for itself, "#option pager" for that option
 * for #help, the name of an environment variable, or "" for the default
 */
const char *editor_command(bool view, bool builtin, const char **from)
{
    const char *editor = NULL;
    const char *source = NULL;
    if (builtin) {
        editor = opt_help_pager;
        if (editor == NULL
            && (editor = env_command("POWWOWHELPPAGER")) != NULL)
            source = "POWWOWHELPPAGER";
    }
    if (editor == NULL && (editor = view ? opt_pager : opt_editor) != NULL)
        source = builtin ? "#option pager" : NULL;
    if (editor == NULL) {
        source = view ? "POWWOWPAGER" : "POWWOWEDITOR";
        editor = env_command(source);
    }
    if (editor == NULL) {
        source = view ? "PAGER" : "EDITOR";
        editor = env_command(source);
    }
    if (editor == NULL) {
        source = "";
        editor = windows_editor(view);
        if (editor == NULL)
            editor = view ? "more" : "vi";
    }
    if (from != NULL)
        *from = source;
    return editor;
}

/* the longest suffix of a temporary file, with its . */
#define SUFFIX_MAX 16

/*
 * a rule of #editsuffix: the temporary file of a text from the MUD whose
 * title matches the regexp 'pattern' ends in 'suffix', where $0 is the
 * title, $1 the match and $2..$9 its groups, as for #action
 */
typedef struct suffixnode {
    struct suffixnode *next;
    char *pattern;                      /* as given, not unescaped */
    char *suffix;
#ifdef USE_REGEXP
    regex_t regexp;
#endif
} suffixnode;

/* the MUD's own rules, which come first, and the user-wide ones, which
 * start as the defaults */
static suffixnode *suffix_mud, *suffix_user;
static bool suffix_user_ready;          /* suffix_user has been set */

#ifdef USE_REGEXP
static const struct {
    const char *pattern, *suffix;
} suffix_defaults[] = {
    { "^Edit mudlle file",            ".mud" },
    { "[.][[:alpha:]][[:alnum:]]*$", "$1" },
};
#define SUFFIX_DEFAULTS (sizeof(suffix_defaults) / sizeof(*suffix_defaults))

/* the default rule with the pattern 'pattern', or -1 */
static int suffix_default(const char *pattern)
{
    for (size_t i = 0; i < SUFFIX_DEFAULTS; ++i) {
        if (strcmp(pattern, suffix_defaults[i].pattern) == 0)
            return (int)i;
    }
    return -1;
}

/*
 * compile the regexp 'pattern', unescaped as #action does, in 're';
 * return false, having said why, if it is not valid
 */
static bool suffix_regcomp(const char *pattern, regex_t *re)
{
    char unesc[BUFSIZE];
    my_strncpy(unesc, pattern, BUFSIZE - 1);
    unescape(unesc);
#ifdef REG_UTF
    /* PCRE2: match UTF-8 characters, not bytes */
    const int cflags = REG_EXTENDED | REG_UTF;
#else
    const int cflags = REG_EXTENDED;
#endif
    int errcode = regcomp(re, unesc, cflags);
    if (errcode == 0)
        return true;
    char why[BUFSIZE];
    regerror(errcode, re, why, sizeof why);
    message_printf("#regexp error: %s\n", why);
    return false;
}

static suffixnode **lookup_suffix(suffixnode **np, const char *pattern)
{
    while (*np != NULL && strcmp((*np)->pattern, pattern) != 0)
        np = &(*np)->next;
    return np;
}
#endif

static void delete_suffixnode(suffixnode **np)
{
    suffixnode *n = *np;
    *np = n->next;
#ifdef USE_REGEXP
    regfree(&n->regexp);
#endif
    free(n->pattern);
    free(n->suffix);
    free(n);
}

/* the user-wide rules, the defaults until they are changed */
static suffixnode **suffix_user_rules(void)
{
    if (suffix_user_ready)
        return &suffix_user;
    suffix_user_ready = true;
#ifdef USE_REGEXP
    suffixnode **tail = &suffix_user;
    for (size_t i = 0; i < SUFFIX_DEFAULTS; ++i) {
        suffixnode *n = xmalloc(sizeof *n);
        if (!suffix_regcomp(suffix_defaults[i].pattern, &n->regexp)) {
            free(n);
            continue;
        }
        n->pattern = xstrdup(suffix_defaults[i].pattern);
        n->suffix = xstrdup(suffix_defaults[i].suffix);
        n->next = NULL;
        *tail = n;
        tail = &n->next;
    }
#endif
    return &suffix_user;
}

/* delete the MUD's own rules of #editsuffix */
void reset_edit_suffixes(void)
{
    while (suffix_mud != NULL)
        delete_suffixnode(&suffix_mud);
}

/* set the user-wide rules of #editsuffix back to the defaults */
void reset_user_edit_suffixes(void)
{
    while (suffix_user != NULL)
        delete_suffixnode(&suffix_user);
    suffix_user_ready = false;
}

/*
 * write the rules of #editsuffix to 'f': the MUD's own, if 'mud', then the
 * user-wide ones, if 'user', those that are not defaults, after the
 * defaults they delete if 'deleted'; return as fprintf() last did, or 1
 */
int save_edit_suffixes(FILE *f, bool mud, bool user, bool deleted)
{
    int failed = 1;
    for (suffixnode *n = mud ? suffix_mud : NULL; n != NULL && failed > 0;
         n = n->next) {
        failed = fprintf(f, "#editsuffix %s=%s\n", n->pattern, n->suffix);
    }
    if (!user)
        return failed;
#ifdef USE_REGEXP
    suffixnode **list = suffix_user_rules();
    for (size_t i = 0; deleted && i < SUFFIX_DEFAULTS && failed > 0; ++i) {
        if (*lookup_suffix(list, suffix_defaults[i].pattern) == NULL) {
            failed = fprintf(f, "#editsuffix %s=\n",
                             suffix_defaults[i].pattern);
        }
    }
    for (suffixnode *n = *list; n != NULL && failed > 0; n = n->next) {
        int i = suffix_default(n->pattern);
        if (i < 0 || strcmp(n->suffix, suffix_defaults[i].suffix) != 0) {
            failed = fprintf(f, "#editsuffix %s=%s\n", n->pattern,
                             n->suffix);
        }
    }
#else
    (void)deleted;
#endif
    return failed;
}

/* list the rules of #editsuffix */
static void show_edit_suffixes(void)
{
    suffixnode *lists[] = { suffix_mud, *suffix_user_rules() };
    const char *kinds[] = { "", "user-wide " };
    for (int i = 0; i < 2; ++i) {
        message_printf("#%s %sedit suffix%s defined%c\n",
                       lists[i] != NULL ? "the following" : "no", kinds[i],
                       (lists[i] != NULL && lists[i]->next == NULL
                        ? " is"
                        : "es are"),
                       lists[i] != NULL ? ':' : '.');
        for (suffixnode *n = lists[i]; n != NULL; n = n->next)
            tty_message("#editsuffix %s=%s\n", n->pattern, n->suffix);
    }
}

/*
 * the #editsuffix command: list the rules, show one, or define or delete
 * one, user-wide in the user-wide settings file, else the MUD's own
 */
void parse_edit_suffix(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        show_edit_suffixes();
        return;
    }
#ifndef USE_REGEXP
    message_printf("#error: regexp not allowed\n");
#else
    char *eq = first_regular(arg, '=');
    if (*eq == '\0') {
        suffixnode *n = *lookup_suffix(&suffix_mud, arg);
        if (n == NULL)
            n = *lookup_suffix(suffix_user_rules(), arg);
        if (n == NULL) {
            wrap_printf("#unknown edit suffix, cannot show: \"%s\"", arg);
            return;
        }
        fmt_next_edit("#editsuffix %s=%s", n->pattern, n->suffix);
        return;
    }
    *eq = '\0';
    const char *suffix = eq + 1;
    if (*arg == '\0') {
        message_printf("#editsuffix: the pattern must be non-empty.\n");
        return;
    }

    bool user = reading_config;
    suffixnode **list = user ? suffix_user_rules() : &suffix_mud;
    const char *kind = user ? "user-wide edit suffix" : "edit suffix";
    suffixnode **np = lookup_suffix(list, arg);
    if (*suffix == '\0') {
        if (*np == NULL) {
            wrap_printf("#unknown %s, cannot delete: \"%s\"", kind, arg);
            return;
        }
        if (opt_info) {
            wrap_printf("#deleting %s: %s=%s", kind, (*np)->pattern,
                        (*np)->suffix);
        }
        delete_suffixnode(np);
        return;
    }
    if (*np != NULL) {
        free((*np)->suffix);
        (*np)->suffix = xstrdup(suffix);
        if (opt_info)
            wrap_printf("#changed %s: %s=%s", kind, arg, suffix);
        return;
    }

    suffixnode *n = xmalloc(sizeof *n);
    if (!suffix_regcomp(arg, &n->regexp)) {
        free(n);
        return;
    }
    n->pattern = xstrdup(arg);
    n->suffix = xstrdup(suffix);
    /* the user-wide ones of the user before the defaults, as they come
     * back when the user-wide settings file is read */
    if (user) {
        np = list;
        while (*np != NULL && suffix_default((*np)->pattern) < 0)
            np = &(*np)->next;
    }
    n->next = *np;
    *np = n;
    if (opt_info)
        wrap_printf("#new %s: %s=%s", kind, arg, suffix);
#endif
}

#ifdef USE_REGEXP
/*
 * the suffix that the rule 'n' gives to the title 'title', which it
 * matched as 'm', in 'buf' of SUFFIX_MAX + 1 bytes; return false if it is
 * not a . then 1 to SUFFIX_MAX - 1 letters, digits, _ and -, as the title
 * comes from the MUD
 */
static bool expand_suffix(const suffixnode *n, const char *title,
                          const regmatch_t *m, char *buf)
{
    size_t len = 0;
    for (const char *p = n->suffix; *p != '\0'; ++p) {
        const char *s = p;
        size_t slen = 1;
        if (*p == '$' && isdigit((unsigned char)p[1])) {
            int i = *++p - '0';
            if (i == 0) {
                s = title;
                slen = strlen(title);
            } else if (i - 1 <= (int)n->regexp.re_nsub
                       && i - 1 < PARAM_COUNT - 1 && m[i - 1].rm_so >= 0) {
                s = title + m[i - 1].rm_so;
                slen = m[i - 1].rm_eo - m[i - 1].rm_so;
            } else {
                slen = 0;
            }
        }
        if (slen > SUFFIX_MAX - len)
            return false;
        memcpy(buf + len, s, slen);
        len += slen;
    }
    buf[len] = '\0';
    if (len < 2 || buf[0] != '.')
        return false;
    for (size_t i = 1; i < len; ++i) {
        unsigned char c = buf[i];
        if (c >= 0x80 || (!isalnum(c) && c != '_' && c != '-'))
            return false;
    }
    return true;
}
#endif

/*
 * the suffix of the temporary file of a text from the MUD with the title
 * 'title', in UTF-8, in 'buf' of SUFFIX_MAX + 1 bytes: as the first rule
 * of #editsuffix whose pattern matches it gives, the MUD's own before the
 * user-wide ones, else .txt; a rule that gives no valid suffix is passed
 * over with a warning
 */
static void edit_suffix(const char *title, char *buf)
{
#ifdef USE_REGEXP
    suffixnode *lists[] = { suffix_mud, *suffix_user_rules() };
    for (int i = 0; i < 2; ++i) {
        for (suffixnode *n = lists[i]; n != NULL; n = n->next) {
            regmatch_t m[PARAM_COUNT - 1];
            if (regexec(&n->regexp, title, PARAM_COUNT - 1, m, 0) != 0)
                continue;
            if (expand_suffix(n, title, m, buf))
                return;
            wrap_printf("#warning: #editsuffix %s=%s gives no valid suffix"
                        " for \"%s\"", n->pattern, n->suffix, title);
        }
    }
#else
    (void)title;
#endif
    strcpy(buf, ".txt");
}

/*
 * print the 'len' bytes of 'text' that the MUD of 'conn' sent to view, as
 * the lines that it sends, without running the actions on them
 */
static void print_view_text(connsess *conn, const char *text, size_t len)
{
    if (len > (SIZE_MAX - 1) / 3) {
        wrap_printf("#warning: text to view too long");
        return;
    }
    char *utf8 = xmalloc(3 * len + 1);
    if (conn != NULL && conn->ideditor_utf8)
        len = utf8_sanitize(text, len, utf8);
    else
        len = latin1_to_utf8(text, len, utf8);
    utf8[len] = '\0';
    for (char *line = utf8; *line != '\0';) {
        char *end = strchr(line, '\n');
        char *next = end != NULL ? end + 1 : line + strlen(line);
        if (end == NULL)
            end = next;
        if (end > line && end[-1] == '\r')
            --end;
        *end = '\0';
        smart_print(line, true);
        line = next;
    }
    free(utf8);
}

/*
 * start an editing session: process the EDIT/VIEW message
 * if view is true, text will be viewed, else edited
 */
void message_edit(connsess *conn, char *text, int msglen, bool view,
                  bool builtin)
{
    const char *errdesc = "#warning: bad MPI edit message\n";

    status(1);

    unsigned int key = ~0U;
    int i = 0;
    if (!view) {
        if (msglen < 1 || text[0] != 'M') {
            tty_puts(errdesc);
            free(text);
            return;
        }
        for (i = 1; i < msglen && isdigit((unsigned char)text[i]); ++i)
            ;
        if (i >= msglen || text[i++] != '\n' || i >= msglen) {
            tty_puts(errdesc);
            free(text);
            return;
        }
        key = strtoul(text + 1, NULL, 10);
    }
    char *descr = text + i;
    while (i < msglen && text[i] != '\n')
        ++i;
    if (i >= msglen) {
        tty_puts(errdesc);
        free(text);
        return;
    }
    text[i++] = '\0';
    /* the description, converted to UTF-8, takes up to 3 bytes per byte */
    if (!builtin && strlen(descr) > (SIZE_MAX - 1) / 3) {
        tty_puts(errdesc);
        free(text);
        return;
    }

    /* without a pager, #help prints the text as its messages, and that of
     * the MUD as its other lines */
    config_refresh(true);       /* such as an editor set in the launcher */
    const char *editor = editor_command(view, builtin, NULL);
    if (view && strcmp(editor, "-") == 0) {
        if (builtin)
            message_printf("%s", text + i);
        else
            print_view_text(conn, text + i, msglen - i);
        free(text);
        return;
    }

    /* the title, which we keep as UTF-8 */
    char *utf8_descr;
    if (builtin) {
        utf8_descr = xstrdup(descr);
    } else {
        size_t dlen = strlen(descr);
        utf8_descr = xmalloc(3 * dlen + 1);
        if (conn != NULL && conn->ideditor_utf8)
            dlen = utf8_sanitize(descr, dlen, utf8_descr);
        else
            dlen = latin1_to_utf8(descr, dlen, utf8_descr);
        utf8_descr[dlen] = '\0';
    }

    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL || *tmpdir == '\0')
        tmpdir = "/tmp";
    /* named after what the text is, as the editor or the pager may show
     * the name, such as in the title of its window */
    char kind[32];
    if (builtin)
        snprintf(kind, sizeof kind, "help");
    else if (view)
        snprintf(kind, sizeof kind, "view");
    else
        snprintf(kind, sizeof kind, "edit-%u", key);
    char suffix[SUFFIX_MAX + 1] = "";
#ifdef HAVE_MKSTEMPS
    if (builtin)
        strcpy(suffix, ".txt");
    else
        edit_suffix(utf8_descr, suffix);
#endif
    /* in Windows mode, a TMPDIR as on Windows takes a \\ */
    char sep = windows_mode && strchr(tmpdir, '/') == NULL ? '\\' : '/';
    char tmpname[BUFSIZE];
    int len = snprintf(tmpname, sizeof tmpname, "%s%cpowwow-%s-XXXXXX%s",
                       tmpdir, sep, kind, suffix);
    int tmpfd = -1;
    if (len < 0 || len >= (int)sizeof tmpname)
        errno = ENAMETOOLONG;
    else
#ifdef HAVE_MKSTEMPS
        tmpfd = mkstemps(tmpname, (int)strlen(suffix));
#else
        tmpfd = mkstemp(tmpname);
#endif
    if (tmpfd < 0) {
        errmsg("cannot create %s", tmpname);
        free(utf8_descr);
        free(text);
        return;
    }
    if (write(tmpfd, text + i, msglen - i) < msglen - i) {
        errmsg("cannot write %s", tmpname);
        free(utf8_descr);
        free(text);
        close(tmpfd);
        unlink(tmpname);
        return;
    }
    close(tmpfd);

    editsess *s = xmalloc(sizeof(editsess));
    s->ctime = time(NULL);
    s->oldsize = msglen - i;
    s->key = key;
    s->conn = (view || builtin) ? NULL : conn;
    s->cancel = false;
    s->out_fd = -1;
    s->out_len = 0;
    /* the editor gets the title as sent */
    char *title = xstrdup(descr);
    s->descr = utf8_descr;
    s->file = xstrdup(tmpname);
    free(text);

    /* send a edit_start message (if wanted) */
    if (edit_sess == NULL && *edit_start != '\0') {
        unsigned generation = conn != NULL ? conn->generation : 0;
        error = NO_ERROR;
        parse_instruction(edit_start, false, false, true);
        history_done = 0;
        if (s->conn != NULL && conn->generation != generation) {
            /* it closed the connection: abort as abort_edit_fd() does */
            wrap_printf("#aborted \"%s\" (%u)", s->descr, s->key);
            unlink(tmpname);
            free(title);
            free(s->descr);
            free(s->file);
            free(s);
            if (edit_sess == NULL && *edit_end != '\0') {
                error = NO_ERROR;
                parse_instruction(edit_end, false, false, true);
                history_done = 0;
            }
            return;
        }
    }

    bool waitforeditor = true;
    if (editor[0] == '&') {
        waitforeditor = false;
        ++editor;
    }

    if (waitforeditor) {
        tty_quit();
        /*
         * ignore SIGINT since interrupting the child would interrupt us
         * too, if we are in the same tty group
         */
        sig_permanent(SIGINT, SIG_IGN);
        sig_permanent(SIGCHLD, SIG_DFL);
    }

    /* a background editor must not write over the screen */
    int out_pipe[2] = { -1, -1 };
    if (!waitforeditor && pipe(out_pipe) < 0) {
        errmsg("cannot capture the output of the editor");
        out_pipe[0] = -1;
        out_pipe[1] = -1;
    }

    int childpid = fork();              /* let's get schizophrenic */
    switch (childpid) {
    case 0: {
        if (out_pipe[1] >= 0) {
            int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, 0);
                close(null_fd);
            }
            dup2(out_pipe[1], 1);
            dup2(out_pipe[1], 2);
            close(out_pipe[0]);
            close(out_pipe[1]);
        }
        /* before setenv(), which may move 'editor' from getenv() */
        char *command;
        if (windows_mode) {
            /* as on Windows: run as #! runs a command, such as by cmd,
             * with the Windows path of the file */
            char path[BUFSIZE];
            command = shell_command(editor,
                                    windows_path(s->file, path, sizeof path));
        } else {
            /* any character of $TMPDIR is safe in "$1" */
            command = xmalloc(strlen(editor) + 6);
            sprintf(command, "%s \"$1\"", editor);
        }
        setenv("TITLE", title, 1);
        execl("/bin/sh", "/bin/sh", "-c", command, "/bin/sh", s->file,
              (char *)NULL);
        exec_failed("the editor");
        break;
    }
    case -1:
        if (waitforeditor) {
            signal_start();
            tty_start();
        }
        errmsg("cannot start the editor");
        if (out_pipe[0] >= 0) {
            close(out_pipe[0]);
            close(out_pipe[1]);
        }
        unlink(tmpname);
        free(title);
        free(s->descr);
        free(s->file);
        free(s);
        return;
    }
    free(title);
    s->pid = childpid;
    if (out_pipe[0] >= 0) {
        close(out_pipe[1]);
        fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(out_pipe[0], F_SETFD, FD_CLOEXEC);
        s->out_fd = out_pipe[0];
    }
    if (waitforeditor) {
        int wpid;
        while ((wpid = waitpid(childpid, NULL, 0)) == -1 && errno == EINTR)
            ;

        signal_start();         /* reset SIGINT and SIGCHLD handlers */
        tty_start();
        /* the terminal echoed the keys typed after the editor restored
         * it and before tty_start(), such as ^P: erase them */
        tty_putc('\r');
        tty_puts(tty_clreoln);

        if (s->conn != NULL) {
            tty_gotoxy(0, lines - 1);
            tty_putc('\n');
        }
        /* the input line at the bottom again, after what the editor or
         * the pager wrote, rather than where it was, as after #! */
        col0 = 0;
        line0 = lines - 1;
        tty_gotoxy(col0, line0);
        tty_puts(tty_clreoln);

        if (wpid == -1)
            errmsg("cannot wait for the editor");
        else
            finish_edit(s);

        free(s->descr);
        free(s->file);
        if (wpid != -1 && edit_sess == NULL && *edit_end != '\0') {
            error = NO_ERROR;
            parse_instruction(edit_end, false, false, true);
            history_done = 0;
        }

        free(s);
    } else {
        s->next = edit_sess;
        edit_sess = s;
    }
}

/*
 * show the complete lines of output from the background editor of
 * 'sp', and with 'all' also an incomplete last line
 */
static void show_edit_output(editsess *sp, bool all)
{
    static const char prefix[] = "#editor: ";
    size_t end = sp->out_len;

    if (!all) {
        while (end > 0 && sp->out_buf[end - 1] != '\n')
            --end;
    }
    if (end == 0)
        return;

    size_t nlines = 1;
    for (size_t i = 0; i < end; ++i) {
        if (sp->out_buf[i] == '\n')
            ++nlines;
    }
    char *msg = xmalloc(end + nlines * sizeof(prefix) + 1);

    char *d = msg;
    for (size_t i = 0; i < end; ) {
        const char *nl = memchr(sp->out_buf + i, '\n', end - i);
        size_t len = (nl != NULL ? (size_t)(nl - sp->out_buf) : end) - i;
        memcpy(d, prefix, sizeof(prefix) - 1);
        d += sizeof(prefix) - 1;
        memcpy(d, sp->out_buf + i, len);
        d += len;
        *d++ = '\n';
        i += len + 1;
    }
    *d = '\0';
    write_message(msg);
    free(msg);

    sp->out_len -= end;
    memmove(sp->out_buf, sp->out_buf + end, sp->out_len);
}

/* show what is left of the output of 'sp' and stop reading it */
static void close_edit_output(editsess *sp)
{
    show_edit_output(sp, true);
    close(sp->out_fd);
    sp->out_fd = -1;
}

/*
 * read the available output of the background editor of 'sp', closing
 * it at the end; return true if there may be more to read
 */
static bool read_edit_output(editsess *sp)
{
    ssize_t n = read(sp->out_fd, sp->out_buf + sp->out_len,
                     sizeof(sp->out_buf) - sp->out_len);
    if (n > 0) {
        sp->out_len += (size_t)n;
        show_edit_output(sp, sp->out_len == sizeof(sp->out_buf));
        return true;
    }
    if (n < 0 && errno == EINTR)
        return true;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;
    close_edit_output(sp);
    return false;
}

int edit_output_fds(fd_set *set, int max_fd)
{
    for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
        if (sp->out_fd >= 0) {
            FD_SET(sp->out_fd, set);
            if (max_fd < sp->out_fd)
                max_fd = sp->out_fd;
        }
    }
    return max_fd;
}

int edit_read_output(const fd_set *set)
{
    int count = 0;
    for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
        if (sp->out_fd >= 0 && FD_ISSET(sp->out_fd, set)) {
            ++count;
            read_edit_output(sp);
        }
    }
    return count;
}

/*
 * Our child has snuffed it. check if it was an editor, and update the
 * session list if that is the case.
 */
void sig_chld_bottomhalf(void)
{
    int pid;
    int ret;
    while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
        /* no need to check WIFSTOPPED, since waitpid() has no WUNTRACED */
        editsess **sp = &edit_sess;
        while (*sp != NULL && (*sp)->pid != pid)
            sp = &(*sp)->next;
        if (*sp != NULL) {
            if ((*sp)->out_fd >= 0) {
                /*
                 * show what the editor printed last; do not wait for
                 * the end, as programs it started may still keep it open
                 */
                while ((*sp)->out_fd >= 0 && read_edit_output(*sp))
                    ;
                if ((*sp)->out_fd >= 0)
                    close_edit_output(*sp);
            }
            finish_edit(*sp);
            editsess *p = *sp;
            *sp = p->next;
            connsess *conn = p->conn;
            free(p->descr);
            free(p->file);
            free(p);

            /* send the edit_end message if this is the last editor... */
            if (edit_sess == NULL && *edit_end != '\0') {
                connsess *old_conn = current_connection;
                current_connection = conn;
                error = NO_ERROR;
                parse_instruction(edit_end, false, false, true);
                history_done = 0;
                current_connection = old_conn;
            }
        }
    }
}
