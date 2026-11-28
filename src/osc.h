/* public things from osc.c */

#ifndef _OSC_H_
#define _OSC_H_

#include <stdbool.h>
#include <stddef.h>

/* the URL schemes of the hyperlinks shown when #option link-schemes is
 * not set */
#define LINK_SCHEMES_DEFAULT "https http mailto"

/* the longest URL of a hyperlink that is shown, as the OSC 8 standard of
 * Mudlet says clients must accept */
#define LINK_URL_MAX 4096

/* the longest parameters of a hyperlink that is shown, such as its id */
#define LINK_PARAMS_MAX 250

/* the longest start of a control string kept for the next read: that of
 * a hyperlink, ESC ] 8 ;, the parameters, ;, the URL and the ESC of
 * ESC \ */
#define OSC_PARTIAL_MAX (4 + LINK_PARAMS_MAX + 1 + LINK_URL_MAX + 1)

/* the longest start of a control sequence (ESC [ ...) kept for the next
 * read */
#define CSI_PARTIAL_MAX 64

/*
 * is 'url' a link to show, with one of the URL schemes in 'schemes',
 * separated by spaces or commas, but never telnet or telnets, which open
 * a connection without asking; at most LINK_URL_MAX bytes of printable
 * ASCII, with no user name before the host, as in
 * https://bank.example@evil.example; and for mailto, with only the
 * subject and the body after the address, which has no ?, & or #?
 */
bool osc_link_allowed(const char *url, size_t len, const char *schemes);

/*
 * the bytes at the end of the 'len' bytes of 'buf' that start a control
 * string, such as a hyperlink (ESC ] 8 ; ...), or a control sequence
 * (ESC [ ...), whose end has not arrived yet, and that wait for it; 0 if
 * there are none, also if a line ends after the start of a control
 * string, as a control string from the MUD ends on its line
 */
size_t osc_partial(const char *buf, size_t len);

/*
 * filter the control strings and the C1 controls in the 'len' bytes of
 * 'buf' in place, and return the length left: of the operating system
 * commands (ESC ] ...), keep the titles of the window, and the
 * hyperlinks (OSC 8) if 'links', those that osc_link_allowed() refuses
 * turned into the end of a hyperlink, so that their text is shown as
 * plain text; drop all the others, such as OSC 52, which writes to the
 * clipboard, and the strings of ESC P, ESC X, ESC ^ and ESC _, such as
 * the queries of the terminal, whose answers would come as typed keys;
 * drop one with no end up to the end of its line; and drop ENQ and the
 * control sequences (ESC [ ...) that make the terminal answer, as if
 * typed, such as with its status, the position of the cursor or the
 * title of the window, or print
 */
size_t osc_filter(char *buf, size_t len, bool links, const char *schemes);

#endif /* _OSC_H_ */
