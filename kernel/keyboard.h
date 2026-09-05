/* =============================================================================
 * SENG21213-OS :: PS/2 Keyboard Driver
 * File   : kernel/keyboard.h + keyboard.c
 * Stage 0: Polling-based keyboard input (no interrupts yet).
 *          In Lecture 9 you will replace this with an IRQ1 handler.
 * ============================================================================*/
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

/* Maximum input line length */
#define KB_BUF_SIZE 256

/* Non-ASCII key codes returned by kb_getchar(). */
#define KB_KEY_UP    ((char)0xF1)
#define KB_KEY_DOWN  ((char)0xF2)
#define KB_KEY_LEFT  ((char)0xF3)
#define KB_KEY_RIGHT ((char)0xF4)
#define KB_KEY_PGUP  ((char)0xF5)
#define KB_KEY_PGDN  ((char)0xF6)

void kb_init(void);

/* Read one character (blocks until a key is pressed) */
char kb_getchar(void);

/* Read a line into buf (up to len-1 chars), NUL-terminated.
 * Echoes characters to VGA. Returns number of chars read.
 * Student TODO (Lecture 9): convert to interrupt-driven. */
int kb_readline(char *buf, int len);

#endif /* KEYBOARD_H */
