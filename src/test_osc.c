/*
 *  test_osc.c  --  unit tests for osc.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdbool.h>
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

static void check_url(const char *url, const char *schemes, bool expect)
{
    if (osc_link_allowed(url, strlen(url), schemes) != expect) {
        printf("FAIL url %s with \"%s\": expected %s\n", url, schemes,
               expect ? "allowed" : "refused");
        ++failures;
    }
}

static void check_filter(const char *what, const char *text, bool show,
                         const char *expect)
{
    char buf[4096];
    size_t len = strlen(text);
    memcpy(buf, text, len);
    len = osc_filter(buf, len, show, LINK_SCHEMES_DEFAULT);
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
#define D LINK_SCHEMES_DEFAULT

int main(void)
{
    check_url("https://example.org/", D, true);
    check_url("HTTP://example.org", D, true);
    check_url("mailto:someone@example.org", D, true);
    check_url("https://example.org/a?b=c@d#e@f", D, true);
    check_url("file:///etc/passwd", D, false);
    check_url("javascript:alert(1)", D, false);
    check_url("ssh://example.org", D, false);
    check_url("https://bank.example@evil.example/", D, false);
    check_url("https://user:pw@example.org/", D, false);
    check_url("https://example.org/a b", D, false);
    check_url("https://ex" "\xc3\xa4" "mple.org/", D, false);
    check_url("https", D, false);
    check_url(":foo", D, false);
    check_url("", D, false);
    check_url("ssh://example.org", "https, ssh", true);
    check_url("telnet://mud.example:4242", "https ssh", false);
    check_url("telnet://mud.example:4242", "https telnet", false);
    check_url("TELNETS://mud.example:4242", "telnets", false);
    check_url("mailto:a@example.org?subject=hi&body=x%20y", D, true);
    check_url("MAILTO:a@example.org?SUBJECT=hi", D, true);
    check_url("mailto:a@example.org,b@example.org", D, true);
    check_url("mailto:a@example.org?cc=b@example.org", D, false);
    check_url("mailto:a@example.org?subject=hi&bcc=b@example.org", D, false);
    check_url("mailto:a%3fb@example.org", D, false);
    check_url("mailto:a%26b@example.org", D, false);
    check_url("mailto:a&b@example.org", D, false);
    check_url("mailto:a@example.org#x", D, false);
    check_url("mailto:a@example.org?", D, false);
    check_url("mailto:", D, false);

    char longurl[LINK_URL_MAX + 2];
    memcpy(longurl, "https://", 8);
    memset(longurl + 8, 'a', sizeof longurl - 9);
    longurl[sizeof longurl - 1] = '\0';
    check_url(longurl, D, false);
    longurl[LINK_URL_MAX] = '\0';
    check_url(longurl, D, true);

    const char *link = (E "]8;;https://example.org" ST "text"
                        E "]8;;" ST);
    check_filter("allowed", link, true, link);
    check_filter("allowed, BEL", E "]8;id=1;http://x\aa" E "]8;;\a", true,
                 E "]8;id=1;http://x\aa" E "]8;;\a");
    check_filter("not shown", link, false, "text");
    check_filter("refused", E "]8;;file:///etc/passwd" ST "text" E "]8;;" ST,
                 true, E "]8;;" ST "text" E "]8;;" ST);
    check_filter("refused, BEL", "a" E "]8;;ssh://x\ab", true,
                 "a" E "]8;;\ab");
    check_filter("bad parameters", E "]8;a b;https://x" ST "t", true,
                 E "]8;;" ST "t");
    check_filter("no URL separator", E "]8;https://x" ST "t", true, "t");
    check_filter("cancelled", E "]8;;https://x\x18t", true, "t");
    check_filter("cut by an escape", E "]8;;https://x" E "[1mt", true,
                 E "[1mt");
    check_filter("no end", "t" E "]8;;https://x", true, "t");
    check_filter("no end on its line", "t" E "]8;;https://x\nnext", true,
                 "t\nnext");
    check_filter("no end on its line, not shown",
                 "t" E "]8;;https://x\nnext", false, "t\nnext");
    check_filter("other escapes", E "[1m" E "]0;title\at", true,
                 E "[1m" E "]0;title\at");
    check_filter("titles", E "]1;i" ST E "]2;t\a", false,
                 E "]1;i" ST E "]2;t\a");
    check_filter("clipboard", "a" E "]52;c;aGk=\ab", true, "ab");
    check_filter("color query", "a" E "]11;?" ST "b", true, "ab");
    check_filter("notification", "a" E "]9;hi" ST "b", true, "ab");
    check_filter("iTerm2", "a" E "]1337;File=x:aGk=\ab", true, "ab");
    check_filter("no number", "a" E "];x\ab", true, "ab");
    check_filter("DCS", "a" E "P+q544e" ST "b", true, "ab");
    check_filter("DCS with BEL", "a" E "Px\ay" ST "b", true, "ab");
    check_filter("APC", "a" E "_Gf=100;AAAA" ST "b", true, "ab");
    check_filter("PM and SOS", "a" E "^x" ST E "Xy" ST "b", true, "ab");
    check_filter("C1 controls", "a\xc2\x9d" "8;;x\xc2\x9c" "b", true,
                 "a8;;xb");
    check_filter("no-break space", "a\xc2\xa0" "b", true, "a\xc2\xa0" "b");

    check_partial("none", "text", 0);
    check_partial("complete", "a" E "]8;;https://x" ST "b", 0);
    check_partial("ESC", "a" E, 1);
    check_partial("ESC ]", "a" E "]", 2);
    check_partial("start", "a" E "]8;;http", 9);
    check_partial("before ST", "a" E "]8;;https://x" E, 15);
    check_partial("other OSC", "a" E "]0;title", 9);
    check_partial("DCS", "a" E "P+q", 4);
    check_partial("CSI", "a" E "[1", 0);
    check_partial("a line after its start", "a" E "]8;;http\nb", 0);

    if (failures != 0) {
        printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    return 0;
}
