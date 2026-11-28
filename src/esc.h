/* escape sequences in text for the terminal (ECMA-48) */

#ifndef _ESC_H_
#define _ESC_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * is 'c' the final byte of a control sequence (ESC [ ... final), as
 * opposed to a parameter or intermediate byte? (independent of the
 * locale, unlike isalpha())
 */
static inline bool is_csi_final(unsigned char c)
{
    return c >= 0x40 && c <= 0x7e;
}

/* is 'c' a parameter or intermediate byte of a control sequence? */
static inline bool is_csi_param(unsigned char c)
{
    return c >= 0x20 && c <= 0x3f;
}

/* is 'c', after ESC, the start of a control string: OSC (]), DCS (P),
 * SOS (X), PM (^) or APC (_)? */
static inline bool is_string_start(uint32_t c)
{
    return c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_';
}

/* where a scan of text is: in the text itself, or in an escape sequence */
enum esc_state {
    ESC_TEXT,
    ESC_START,                  /* after ESC */
    ESC_CSI,                    /* in ESC [ ... final */
    ESC_STRING,                 /* in a control string: ESC ] ... (OSC),
                                 * such as a hyperlink, or ESC P, ESC X,
                                 * ESC ^ or ESC _ ... */
};

/*
 * advance 'state' over the character 'c', and return whether 'c' belongs
 * to an escape sequence, which takes no room on screen: a control
 * sequence, ESC [ ... final, a control string, ended by ESC \, or for an
 * OSC also by BEL, or ESC and another character
 */
static inline bool esc_scan(enum esc_state *state, uint32_t c)
{
    switch (*state) {
    case ESC_TEXT:
        if (c != '\033')
            return false;
        *state = ESC_START;
        break;
    case ESC_START:
        *state = (c == '[' ? ESC_CSI
                  : is_string_start(c) ? ESC_STRING
                  : ESC_TEXT);
        break;
    case ESC_CSI:
        if (c <= 0x7f && is_csi_final(c))
            *state = ESC_TEXT;
        break;
    case ESC_STRING:
        /* ESC starts ESC \, the string terminator; CAN and SUB cancel */
        if (c == '\033')
            *state = ESC_START;
        else if (c == '\a' || c == 0x18 || c == 0x1a)
            *state = ESC_TEXT;
        break;
    }
    return true;
}

#endif /* _ESC_H_ */
