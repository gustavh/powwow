/*
 *  osc.c  --  the control strings in the text from the MUD that reach
 *             the terminal
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

#include "esc.h"
#include "osc.h"

static bool is_ascii_digit(char c)
{
    return c >= '0' && c <= '9';
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

size_t osc_partial(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; ) {
        if (buf[i] != '\033') {
            ++i;
            continue;
        }
        if (i + 1 == len)
            return 1;           /* it may start one */
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
 * put the control string 's', of 'n' bytes, which ends with ESC \ if
 * 'st', else with BEL, at 'out', as osc_filter() says, and return how
 * many bytes it takes there
 */
static size_t put_string(char *out, const char *s, size_t n, bool st)
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
    default:
        return 0;
    }
}

size_t osc_filter(char *buf, size_t len)
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
        if (c != '\033' || i + 1 == len
            || !is_string_start((unsigned char)buf[i + 1])) {
            buf[out++] = buf[i++];
            continue;
        }
        enum string_end end;
        size_t n = string_len(buf + i, len - i, &end);
        if (end == STRING_BEL || end == STRING_ST)
            out += put_string(buf + out, buf + i, n, end == STRING_ST);
        i += n;
    }
    return out;
}
