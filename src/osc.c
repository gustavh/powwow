/*
 *  osc.c  --  the control strings and sequences in the text from the
 *             MUD, such as hyperlinks (OSC 8), that reach the terminal
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "esc.h"
#include "osc.h"

static bool is_ascii_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_ascii_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* the value of the hexadecimal digit 'c', or -1 */
static int hex_value(char c)
{
    if (is_ascii_digit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* are the 'len' bytes at 's' printable ASCII, without spaces? */
static bool is_graphic(const char *s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (s[i] <= ' ' || s[i] > '~')
            return false;
    }
    return true;
}

/* is the scheme of 'len' bytes at 'scheme' one of the words of 'list',
 * separated by spaces or commas, in any case? */
static bool scheme_listed(const char *scheme, size_t len, const char *list)
{
    static const char seps[] = " ,";
    for (list += strspn(list, seps); *list != '\0';
         list += strspn(list, seps)) {
        size_t n = strcspn(list, seps);
        if (n == len && strncasecmp(list, scheme, len) == 0)
            return true;
        list += n;
    }
    return false;
}

/* do the 'len' bytes at 's', with their %xx escapes decoded, have none
 * of the characters of 'chars'? */
static bool decoded_lacks(const char *s, size_t len, const char *chars)
{
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '%' && len - i >= 3 && hex_value(s[i + 1]) >= 0
            && hex_value(s[i + 2]) >= 0) {
            c = (char)(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
            i += 2;
        }
        if (c != '\0' && strchr(chars, c) != NULL)
            return false;
    }
    return true;
}

/*
 * is the mailto URL of 'len' bytes at 's', after "mailto:", one to show,
 * as the OSC 8 standard of Mudlet says: an address with no ?, & or #,
 * also once decoded, and after it, if anything, only the subject and the
 * body, and no fragment
 */
static bool mailto_allowed(const char *s, size_t len)
{
    if (memchr(s, '#', len) != NULL)
        return false;
    const char *query = memchr(s, '?', len);
    size_t addrlen = query != NULL ? (size_t)(query - s) : len;
    if (addrlen == 0 || !decoded_lacks(s, addrlen, "?&#"))
        return false;
    if (query == NULL)
        return true;
    const char *end = s + len;
    for (const char *p = query + 1; p <= end; ) {
        const char *amp = memchr(p, '&', end - p);
        if (amp == NULL)
            amp = end;
        const char *eq = memchr(p, '=', amp - p);
        size_t namelen = (eq != NULL ? eq : amp) - p;
        if (!(namelen == 7 && strncasecmp(p, "subject", 7) == 0)
            && !(namelen == 4 && strncasecmp(p, "body", 4) == 0))
            return false;
        p = amp + 1;
    }
    return true;
}

bool osc_link_allowed(const char *url, size_t len, const char *schemes)
{
    if (len == 0 || len > LINK_URL_MAX || !is_graphic(url, len))
        return false;

    /* the scheme: a letter, then letters, digits, +, - and . (RFC 3986) */
    size_t i = 0;
    if (!is_ascii_alpha(url[i]))
        return false;
    while (i < len && (is_ascii_alpha(url[i]) || is_ascii_digit(url[i])
                       || url[i] == '+' || url[i] == '-' || url[i] == '.'))
        ++i;
    if (i == len || url[i] != ':' || !scheme_listed(url, i, schemes)
        || scheme_listed(url, i, "telnet telnets"))
        return false;

    const char *rest = url + i + 1;
    size_t restlen = len - i - 1;
    if (i == 6 && strncasecmp(url, "mailto", 6) == 0)
        return mailto_allowed(rest, restlen);

    /* the host, after //, with no user name, which could pass off one
     * host as another */
    if (restlen >= 2 && rest[0] == '/' && rest[1] == '/') {
        rest += 2;
        restlen -= 2;
        size_t hostlen = 0;
        while (hostlen < restlen && strchr("/?#", rest[hostlen]) == NULL)
            ++hostlen;
        if (memchr(rest, '@', hostlen) != NULL)
            return false;
    }
    return true;
}

/* how a control string ends */
enum string_end {
    STRING_BEL,                 /* with BEL, as an OSC may */
    STRING_ST,                  /* with ESC \ */
    STRING_BROKEN,              /* cancelled, cut short by another escape
                                 * sequence, or with no end on its line */
    STRING_OPEN,                /* not yet, at the end of the text */
};

/*
 * the length of the control string at the start of the 'len' bytes of
 * 'buf', ESC and a character for which is_string_start() is true, and how
 * it ends in 'end'
 */
static size_t string_len(const char *buf, size_t len, enum string_end *end)
{
    bool osc = buf[1] == ']';
    for (size_t i = 2; i < len; ++i) {
        char c = buf[i];
        if (c == '\033') {
            if (i + 1 == len) {
                *end = STRING_OPEN;
                return len;
            }
            *end = buf[i + 1] == '\\' ? STRING_ST : STRING_BROKEN;
            return *end == STRING_ST ? i + 2 : i;
        }
        if (c == '\a' && osc) {
            *end = STRING_BEL;
            return i + 1;
        }
        if (c == 0x18 || c == 0x1a) {
            *end = STRING_BROKEN;
            return i + 1;
        }
        if (c == '\n') {
            *end = STRING_BROKEN;
            return i;
        }
    }
    *end = STRING_OPEN;
    return len;
}

/* what makes a control sequence (ESC [ ...) what it is */
struct csi {
    char private;               /* <, =, > or ? before the parameters */
    char inter;                 /* the last intermediate byte, ! to / */
    char final;                 /* or '\0' if it has none */
};

/*
 * the length of the control sequence (ESC [ ...) at the start of the
 * 'len' bytes of 'buf', and what it is in 'csi', with no final byte if
 * an ESC, CAN, SUB or a byte that is not ASCII ends it; 0 if it has not
 * ended there. The terminal runs the other C0 controls in it and goes on
 * with the sequence, so they are part of it here.
 */
static size_t csi_len(const char *buf, size_t len, struct csi *csi)
{
    *csi = (struct csi){ 0 };
    for (size_t i = 2; i < len; ++i) {
        unsigned char c = buf[i];
        if (c >= 0x40 && c <= 0x7e) {
            csi->final = (char)c;
            return i + 1;
        }
        if (c == '\033' || c == 0x18 || c == 0x1a || c > 0x7e)
            return i;
        if (i == 2 && c >= '<' && c <= '?')
            csi->private = (char)c;
        else if (c >= 0x20 && c <= 0x2f)
            csi->inter = (char)c;
    }
    return 0;
}

/*
 * does the control sequence 'csi' make the terminal answer, as if typed,
 * or print? Those that ask for its attributes (c), its status and the
 * position of the cursor (n), its parameters (x), the window operations
 * (t), which also report the title, the requests of a mode ($ p) and of
 * the presentation state ($ w, $ u), and of the version (> q), the
 * queries of the keyboard modes (? m, ? u, & u), of the graphics (? S),
 * of the checksum of an area (* y) and of the SGR of an area (# |), and
 * the media copy (i), which prints
 */
static bool makes_terminal_answer(const struct csi *csi)
{
    switch (csi->final) {
    case 'c': case 'n': case 'x': case 't': case 'i':
        return true;
    case 'p': case 'w':
        return csi->inter == '$';
    case 'q':
        return csi->private == '>';
    case 'm': case 'S':
        return csi->private == '?';
    case 'u':
        return (csi->private == '?' || csi->inter == '$'
                || csi->inter == '&');
    case 'y':
        return csi->inter == '*';
    case '|':
        return csi->inter == '#';
    default:
        return false;
    }
}

size_t osc_partial(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; ) {
        if (buf[i] != '\033') {
            ++i;
            continue;
        }
        if (i + 1 == len)
            return 1;           /* it may start one */
        if (buf[i + 1] == '[') {
            struct csi csi;
            size_t n = csi_len(buf + i, len - i, &csi);
            if (n == 0)
                return len - i <= CSI_PARTIAL_MAX ? len - i : 0;
            i += n;
            continue;
        }
        if (!is_string_start((unsigned char)buf[i + 1])) {
            ++i;
            continue;
        }
        enum string_end end;
        size_t n = string_len(buf + i, len - i, &end);
        if (end == STRING_OPEN)
            return n <= OSC_PARTIAL_MAX ? n : 0;
        i += n;
    }
    return 0;
}

/*
 * put the hyperlink 's', of 'n' bytes, with 'body' of 'bodylen' bytes
 * between ESC ] 8 ; and its end, ESC \ if 'st', else BEL, at 'out', as
 * osc_filter() says, and return how many bytes it takes there
 */
static size_t put_link(char *out, const char *s, size_t n, const char *body,
                       size_t bodylen, bool st, const char *schemes)
{
    static const char end_bel[] = "\033]8;;\a";
    static const char end_st[] = "\033]8;;\033\\";

    const char *semi = memchr(body, ';', bodylen);
    if (semi == NULL)
        return 0;
    size_t plen = semi - body;
    const char *url = semi + 1;
    size_t urllen = bodylen - plen - 1;
    if (plen <= LINK_PARAMS_MAX && is_graphic(body, plen)
        && (urllen == 0 || osc_link_allowed(url, urllen, schemes))) {
        memmove(out, s, n);
        return n;
    }
    /* the text goes on as plain text, not as part of the hyperlink
     * before it */
    const char *end = st ? end_st : end_bel;
    size_t endlen = st ? sizeof end_st - 1 : sizeof end_bel - 1;
    memcpy(out, end, endlen);
    return endlen;
}

/*
 * put the control string 's', of 'n' bytes, which ends with ESC \ if
 * 'st', else with BEL, at 'out', as osc_filter() says, and return how
 * many bytes it takes there
 */
static size_t put_string(char *out, const char *s, size_t n, bool st,
                         bool links, const char *schemes)
{
    if (s[1] != ']')
        return 0;
    const char *body = s + 2;
    size_t bodylen = n - 2 - (st ? 2 : 1);
    /* the number of the command, up to ; or the end */
    size_t i = 0;
    while (i < bodylen && is_ascii_digit(body[i]))
        ++i;
    if (i != 1 || (i < bodylen && body[i] != ';'))
        return 0;
    switch (body[0]) {
    case '0':                   /* the title of the window and its icon */
    case '1':
    case '2':
        memmove(out, s, n);
        return n;
    case '8':
        if (!links || i == bodylen)
            return 0;
        return put_link(out, s, n, body + 2, bodylen - 2, st, schemes);
    default:
        return 0;
    }
}

size_t osc_filter(char *buf, size_t len, bool links, const char *schemes)
{
    size_t out = 0;
    for (size_t i = 0; i < len; ) {
        unsigned char c = buf[i];
        /* a C1 control, U+0080 to U+009F, which some terminals take as
         * the start of a control string too */
        if (c == 0xc2 && i + 1 < len && (unsigned char)buf[i + 1] >= 0x80
            && (unsigned char)buf[i + 1] <= 0x9f) {
            i += 2;
            continue;
        }
        /* ENQ, to which the terminal answers with its answerback */
        if (c == 0x05) {
            ++i;
            continue;
        }
        if (c == '\033' && i + 1 < len && buf[i + 1] == '[') {
            struct csi csi;
            size_t n = csi_len(buf + i, len - i, &csi);
            if (n == 0)
                n = len - i;    /* longer than osc_partial() keeps */
            if (!makes_terminal_answer(&csi)) {
                memmove(buf + out, buf + i, n);
                out += n;
            }
            i += n;
            continue;
        }
        if (c != '\033' || i + 1 == len
            || !is_string_start((unsigned char)buf[i + 1])) {
            buf[out++] = buf[i++];
            continue;
        }
        enum string_end end;
        size_t n = string_len(buf + i, len - i, &end);
        if (end == STRING_BEL || end == STRING_ST)
            out += put_string(buf + out, buf + i, n, end == STRING_ST, links,
                              schemes);
        i += n;
    }
    return out;
}
