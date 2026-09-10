/* =============================================================================
 * SENG21213-OS :: PS/2 Keyboard Driver
 * File   : kernel/keyboard.c
 * Stage  : 0 - Full Keyboard Extension
 *
 * Features:
 *   - PS/2 polling keyboard input
 *   - Scancode Set 1
 *   - Shift
 *   - Caps Lock
 *   - Ctrl
 *   - Alt / AltGr
 *   - Extended 0xE0 scancodes
 *   - Command history and navigation keys
 *
 * Lecture reference:
 *   L08 §3 - Keyboard input and I/O
 * ============================================================================*/
#include "keyboard.h"
#include "vga.h"
#include "../include/types.h"

#define KB_DATA_PORT   0x60
#define KB_STATUS_PORT 0x64
#define KB_STATUS_OBF  0x01
#define HISTORY_SIZE   20

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static const char sc_ascii[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,'7','8','9','-',
    '4','5','6','+','1','2','3','0','.',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char sc_ascii_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static bool shift_held = false;
static bool caps_lock = false;
static bool ctrl_held = false;
static bool alt_held = false;
static bool altgr_held = false;
static bool extended_code = false;

static char history[HISTORY_SIZE][KB_BUF_SIZE];
static int history_count = 0;
static int history_next = 0;

static int k_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void k_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0') { }
}

static bool k_streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void history_add(const char *line)
{
    if (line[0] == '\0') return;

    if (history_count > 0) {
        int last = (history_next + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (k_streq(history[last], line)) return;
    }

    k_strcpy(history[history_next], line);
    history_next = (history_next + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

static const char *history_get(int position)
{
    int oldest;
    int index;

    if (position < 0 || position >= history_count) return (const char *)0;

    oldest = (history_next - history_count + HISTORY_SIZE) % HISTORY_SIZE;
    index = (oldest + position) % HISTORY_SIZE;
    return history[index];
}

static char apply_caps_shift(char normal, char shifted)
{
    bool is_letter = (normal >= 'a' && normal <= 'z');

    if (!is_letter) return shift_held ? shifted : normal;
    if (caps_lock == shift_held) return normal;
    return shifted;
}

static char altgr_translate(uint8_t sc)
{
    switch (sc) {
        case 0x03: return '@';
        case 0x04: return '#';
        case 0x1A: return '{';
        case 0x1B: return '}';
        case 0x27: return '~';
        case 0x2B: return '|';
        default: return 0;
    }
}

static char handle_extended_press(uint8_t sc)
{
    if (sc == 0x1D) {
        ctrl_held = true;
        return 0;
    }

    if (sc == 0x38) {
        altgr_held = true;
        alt_held = true;
        return 0;
    }

    switch (sc) {
        case 0x48: return KB_KEY_UP;
        case 0x50: return KB_KEY_DOWN;
        case 0x4B: return KB_KEY_LEFT;
        case 0x4D: return KB_KEY_RIGHT;
        case 0x49: return KB_KEY_PGUP;
        case 0x51: return KB_KEY_PGDN;
        default: return 0;
    }
}

static void handle_extended_release(uint8_t sc)
{
    if (sc == 0x1D) ctrl_held = false;
    if (sc == 0x38) {
        altgr_held = false;
        alt_held = false;
    }
}

void kb_init(void)
{
    while (inb(KB_STATUS_PORT) & KB_STATUS_OBF) {
        inb(KB_DATA_PORT);
    }

    shift_held = false;
    caps_lock = false;
    ctrl_held = false;
    alt_held = false;
    altgr_held = false;
    extended_code = false;
    history_count = 0;
    history_next = 0;
}

char kb_getchar(void)
{
    while (true) {
        uint8_t sc;

        while (!(inb(KB_STATUS_PORT) & KB_STATUS_OBF)) { }
        sc = inb(KB_DATA_PORT);

        if (sc == 0xE0) {
            extended_code = true;
            continue;
        }

        if (extended_code) {
            char key;
            extended_code = false;

            if (sc & 0x80) {
                handle_extended_release(sc & 0x7F);
                continue;
            }

            key = handle_extended_press(sc);
            if (key) return key;
            continue;
        }

        if (sc & 0x80) {
            uint8_t release = sc & 0x7F;
            if (release == 0x2A || release == 0x36) shift_held = false;
            else if (release == 0x1D) ctrl_held = false;
            else if (release == 0x38) alt_held = false;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) {
            shift_held = true;
            continue;
        }

        if (sc == 0x1D) {
            ctrl_held = true;
            continue;
        }

        if (sc == 0x38) {
            alt_held = true;
            altgr_held = false;
            continue;
        }

        if (sc == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }

        if (sc >= 128) continue;

        {
            char normal = sc_ascii[sc];
            char shifted = sc_ascii_shift[sc];

            if (!normal && !shifted) continue;

            if (ctrl_held && normal >= 'a' && normal <= 'z') {
                return (char)(normal - 'a' + 1);
            }

            if (altgr_held) {
                char altgr = altgr_translate(sc);
                if (altgr) return altgr;
            }

            return apply_caps_shift(normal, shifted);
        }
    }
}

static void erase_input(int count)
{
    while (count-- > 0) vga_putchar('\b');
}

static int copy_history_to_buffer(char *buf, int len, int position)
{
    const char *src = history_get(position);
    int n;

    if (!src) return 0;

    n = k_strlen(src);
    if (n >= len) n = len - 1;
    for (int i = 0; i < n; i++) buf[i] = src[i];
    buf[n] = '\0';
    return n;
}

int kb_readline(char *buf, int len)
{
    int i = 0;
    int history_pos = history_count;

    if (len <= 0) return 0;
    buf[0] = '\0';

    while (i < len - 1) {
        char c = kb_getchar();

        if (c == KB_KEY_UP) {
            if (history_count > 0 && history_pos > 0) {
                erase_input(i);
                history_pos--;
                i = copy_history_to_buffer(buf, len, history_pos);
                vga_puts(buf);
            }
            continue;
        }

        if (c == KB_KEY_DOWN) {
            if (history_pos < history_count) {
                erase_input(i);
                history_pos++;
                if (history_pos == history_count) {
                    i = 0;
                    buf[0] = '\0';
                } else {
                    i = copy_history_to_buffer(buf, len, history_pos);
                }
                vga_puts(buf);
            }
            continue;
        }

        if (c == KB_KEY_PGUP) {
            vga_scroll_page(-1);
            continue;
        }

        if (c == KB_KEY_PGDN) {
            vga_scroll_page(1);
            continue;
        }

        if (c == KB_KEY_LEFT || c == KB_KEY_RIGHT) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            history_add(buf);
            vga_putchar('\n');
            break;
        }

        if (c == '\b') {
            if (i > 0) {
                i--;
                buf[i] = '\0';
                vga_putchar('\b');
            }
            continue;
        }

        if ((uint8_t)c < 32) continue;

        buf[i++] = c;
        buf[i] = '\0';
        vga_putchar(c);
        history_pos = history_count;
    }

    buf[i] = '\0';
    return i;
}
