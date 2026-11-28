/* public things from osc.c */

#ifndef _OSC_H_
#define _OSC_H_

#include <stddef.h>

/* the longest start of a control string kept for the next read */
#define OSC_PARTIAL_MAX 4096

/*
 * the bytes at the end of the 'len' bytes of 'buf' that start a control
 * string whose end has not arrived yet, and that wait for it; 0 if there
 * are none, also if a line ends after its start, as a control string
 * from the MUD ends on its line
 */
size_t osc_partial(const char *buf, size_t len);

/*
 * filter the control strings and the C1 controls in the 'len' bytes of
 * 'buf' in place, and return the length left: of the operating system
 * commands (ESC ] ...), keep the titles of the window; drop all the
 * others, such as OSC 52, which writes to the clipboard, and the strings
 * of ESC P, ESC X, ESC ^ and ESC _, such as the queries of the terminal,
 * whose answers would come as typed keys; drop one with no end up to the
 * end of its line
 */
size_t osc_filter(char *buf, size_t len);

#endif /* _OSC_H_ */
