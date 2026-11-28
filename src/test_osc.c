/*
 *  test_osc.c  --  unit tests for osc.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <string.h>

#include "osc.h"

static int failures;

static void print_escaped(const char *s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '\033')
            printf("\\e");
        else if (s[i] == '\a')
            printf("\\a");
        else
            printf("%c", s[i]);
    }
}

static void check_filter(const char *what, const char *text,
                         const char *expect)
{
    char buf[4096];
    size_t len = strlen(text);
    memcpy(buf, text, len);
    len = osc_filter(buf, len);
    if (len != strlen(expect) || memcmp(buf, expect, len) != 0) {
        printf("FAIL filter %s: got \"", what);
        print_escaped(buf, len);
        printf("\"\n");
        ++failures;
    }
}

static void check_partial(const char *what, const char *text, size_t expect)
{
    size_t n = osc_partial(text, strlen(text));
    if (n != expect) {
        printf("FAIL partial %s: got %zu, expected %zu\n", what, n, expect);
        ++failures;
    }
}

#define E "\033"
#define ST E "\\"

int main(void)
{
    check_filter("plain text", "text", "text");
    check_filter("SGR", E "[1mt" E "[m", E "[1mt" E "[m");
    check_filter("titles", E "]0;t\a" E "]1;i" ST E "]2;t\a",
                 E "]0;t\a" E "]1;i" ST E "]2;t\a");
    check_filter("clipboard", "a" E "]52;c;aGk=\ab", "ab");
    check_filter("color query", "a" E "]11;?" ST "b", "ab");
    check_filter("notification", "a" E "]9;hi" ST "b", "ab");
    check_filter("iTerm2", "a" E "]1337;File=x:aGk=\ab", "ab");
    check_filter("no number", "a" E "];x\ab", "ab");
    check_filter("DCS", "a" E "P+q544e" ST "b", "ab");
    check_filter("DCS with BEL", "a" E "Px\ay" ST "b", "ab");
    check_filter("APC", "a" E "_Gf=100;AAAA" ST "b", "ab");
    check_filter("PM and SOS", "a" E "^x" ST E "Xy" ST "b", "ab");
    check_filter("cancelled", "a" E "]52;c;x\x18t", "at");
    check_filter("cut by an escape", "a" E "]52;c;x" E "[1mt", "a" E "[1mt");
    check_filter("no end", "t" E "]0;x", "t");
    check_filter("no end on its line", "t" E "]0;x\nnext", "t\nnext");
    check_filter("C1 controls", "a\xc2\x9d" "0;x\xc2\x9c" "b", "a0;xb");
    check_filter("no-break space", "a\xc2\xa0" "b", "a\xc2\xa0" "b");

    check_partial("none", "text", 0);
    check_partial("complete", "a" E "]0;t" ST "b", 0);
    check_partial("ESC", "a" E, 1);
    check_partial("ESC ]", "a" E "]", 2);
    check_partial("OSC", "a" E "]0;title", 9);
    check_partial("before ST", "a" E "]0;t" E, 6);
    check_partial("DCS", "a" E "P+q", 4);
    check_partial("CSI", "a" E "[1", 0);
    check_partial("a line after its start", "a" E "]0;t\nb", 0);

    if (failures != 0) {
        printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    return 0;
}
