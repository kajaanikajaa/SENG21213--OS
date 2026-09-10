/* =============================================================================
 * SENG21213-OS :: VGA Text-Mode Driver
 * File   : kernel/vga.c
 * Purpose: Implements the VGA 80×25 colour text-mode output driver.
 *          Direct memory-mapped I/O – no BIOS calls in protected mode.
 * ============================================================================*/
#include "vga.h"
#include "../include/types.h"

#define SCROLLBACK_LINES 200

static uint16_t text_buffer[SCROLLBACK_LINES][VGA_COLS];
static int cursor_line = 0;
static int cursor_col = 0;
static int first_line = 0;
static int line_count = 1;
static int view_top = 0;
static bool follow_tail = true;
static uint8_t cur_attr = 0;

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int last_line(void)
{
    return first_line + line_count - 1;
}

static int line_slot(int line)
{
    int slot = line % SCROLLBACK_LINES;
    if (slot < 0) slot += SCROLLBACK_LINES;
    return slot;
}

static void enable_hw_cursor(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

static void disable_hw_cursor(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void update_hw_cursor(void)
{
    int screen_row;
    uint16_t pos;

    if (!follow_tail) {
        disable_hw_cursor();
        return;
    }

    screen_row = cursor_line - view_top;

    if (screen_row < 0 || screen_row >= VGA_ROWS) {
        disable_hw_cursor();
        return;
    }

    pos = (uint16_t)(screen_row * VGA_COLS + cursor_col);

    enable_hw_cursor();

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void fill_line(int line, uint8_t attr)
{
    int slot = line_slot(line);
    for (int c = 0; c < VGA_COLS; c++) {
        text_buffer[slot][c] = (uint16_t)(((uint16_t)attr << 8) | ' ');
    }
}

static void render_view(void)
{
    volatile uint16_t *vga = VGA_ADDR;

    for (int r = 0; r < VGA_ROWS; r++) {
        int line = view_top + r;
        bool available = line >= first_line && line <= last_line();

        for (int c = 0; c < VGA_COLS; c++) {
            if (available) {
                vga[r * VGA_COLS + c] = text_buffer[line_slot(line)][c];
            } else {
                vga[r * VGA_COLS + c] = (uint16_t)(((uint16_t)cur_attr << 8) | ' ');
            }
        }
    }

    update_hw_cursor();
}

static void ensure_tail_view(void)
{
    int tail_top = cursor_line - (VGA_ROWS - 1);
    if (tail_top < first_line) tail_top = first_line;
    view_top = tail_top;
}

static void new_line(void)
{
    cursor_line++;
    cursor_col = 0;

    if (line_count < SCROLLBACK_LINES) {
        line_count++;
    } else {
        first_line++;
    }

    fill_line(cursor_line, cur_attr);

    if (follow_tail) ensure_tail_view();
}

static void vga_write_current(char c)
{
    text_buffer[line_slot(cursor_line)][cursor_col] =
        (uint16_t)(((uint16_t)cur_attr << 8) | (uint8_t)c);
}

void vga_init(void)
{
    cur_attr = VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK);
    cursor_line = 0;
    cursor_col = 0;
    first_line = 0;
    line_count = 1;
    view_top = 0;
    follow_tail = true;

    for (int r = 0; r < SCROLLBACK_LINES; r++) {
        fill_line(r, cur_attr);
    }
    render_view();
}



void vga_clear(vga_color_t bg)
{
    cur_attr = VGA_ATTR(VGA_LIGHT_GREY, bg);
    cursor_line = 0;
    cursor_col = 0;
    first_line = 0;
    line_count = 1;
    view_top = 0;
    follow_tail = true;

    for (int r = 0; r < SCROLLBACK_LINES; r++) {
        fill_line(r, cur_attr);
    }

    render_view();
}

void vga_set_color(vga_color_t fg, vga_color_t bg)
{
    cur_attr = VGA_ATTR(fg, bg);
}

vga_color_t vga_get_fg(void)
{
    return (vga_color_t)(cur_attr & 0x0F);
}

vga_color_t vga_get_bg(void)
{
    return (vga_color_t)((cur_attr >> 4) & 0x0F);
}

void vga_putchar(char c)
{
    if (!follow_tail) {
        follow_tail = true;
        ensure_tail_view();
    }

    if (c == '\n') {
        new_line();
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        int next = (cursor_col + 8) & ~7;
        if (next >= VGA_COLS) new_line();
        else cursor_col = next;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            vga_write_current(' ');
        }
    } else {
        if (cursor_col >= VGA_COLS) new_line();
        vga_write_current(c);
        cursor_col++;
        if (cursor_col >= VGA_COLS) new_line();
    }

    if (follow_tail) {
        ensure_tail_view();
        render_view();
    } else {
        render_view();
    }
}

static int ansi_value(const char *s, int *index)
{
    int value = 0;
    bool have_digit = false;

    while (s[*index] >= '0' && s[*index] <= '9') {
        value = value * 10 + (s[*index] - '0');
        have_digit = true;
        (*index)++;
    }

    return have_digit ? value : 0;
}

static vga_color_t ansi_fg(int code)
{
    if (code >= 30 && code <= 37) return (vga_color_t)(code - 30);
    return (vga_color_t)(code - 90 + 8);
}

static vga_color_t ansi_bg(int code)
{
    if (code >= 40 && code <= 47) return (vga_color_t)(code - 40);
    return (vga_color_t)(code - 100 + 8);
}

static void ansi_apply(const char *seq, int end)
{
    int i = 0;
    bool had_parameter = false;
    vga_color_t fg = vga_get_fg();
    vga_color_t bg = vga_get_bg();

    while (i < end) {
        int code;

        if (seq[i] == ';') {
            i++;
            continue;
        }

        code = ansi_value(seq, &i);
        had_parameter = true;

        if (code == 0) {
            fg = VGA_LIGHT_GREY;
            bg = VGA_BLACK;
        } else if (code >= 30 && code <= 37) {
            fg = ansi_fg(code);
        } else if (code >= 90 && code <= 97) {
            fg = ansi_fg(code);
        } else if (code >= 40 && code <= 47) {
            bg = ansi_bg(code);
        } else if (code >= 100 && code <= 107) {
            bg = ansi_bg(code);
        }

        if (i < end && seq[i] == ';') i++;
    }

    if (!had_parameter) {
        fg = VGA_LIGHT_GREY;
        bg = VGA_BLACK;
    }

    vga_set_color(fg, bg);
}

void vga_puts(const char *str)
{
    if (!str) return;

    while (*str) {
        if ((uint8_t)str[0] == 0x1B && str[1] == '[') {
            int i = 2;
            while (str[i] && !((str[i] >= '@') && (str[i] <= '~'))) i++;

            if (str[i] == 'm') {
                ansi_apply(str + 2, i - 2);
                str += i + 1;
                continue;
            }
        }

        vga_putchar(*str++);
    }
}

void vga_puts_color(const char *str, vga_color_t fg, vga_color_t bg)
{
    uint8_t saved = cur_attr;
    vga_set_color(fg, bg);
    vga_puts(str);
    cur_attr = saved;
}

static void ensure_line_exists(int line)
{
    if (line < first_line)
        return;

    while (line > last_line() &&
           line_count < SCROLLBACK_LINES)
    {
        int new_line_no =
            first_line + line_count;

        fill_line(
            new_line_no,
            cur_attr
        );

        line_count++;
    }

    while (line > last_line())
    {
        first_line++;

        fill_line(
            first_line + line_count - 1,
            cur_attr
        );
    }
}

void vga_set_cursor(int row, int col)
{
    if (row < 0) row = 0;
    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    if (col < 0) col = 0;
    if (col >= VGA_COLS) col = VGA_COLS - 1;

    cursor_line = view_top + row;
    if (cursor_line < first_line)
        cursor_line = first_line;

    ensure_line_exists(cursor_line);

    cursor_col = col;
    follow_tail = true;
    ensure_tail_view();
    render_view();
}

void vga_scroll_page(int direction)
{
    int max_top = last_line() - (VGA_ROWS - 1);

    if (max_top < first_line) max_top = first_line;

    if (direction < 0) {
        view_top -= (VGA_ROWS - 1);
        if (view_top < first_line) view_top = first_line;
        follow_tail = (view_top == max_top);
    } else if (direction > 0) {
        view_top += (VGA_ROWS - 1);
        if (view_top >= max_top) {
            view_top = max_top;
            follow_tail = true;
        } else {
            follow_tail = false;
        }
    }

    render_view();
}

static void print_uint(uint32_t n, int base)
{
    char buf[32];
    int i = 0;

    if (n == 0) {
        vga_putchar('0');
        return;
    }

    while (n > 0) {
        int r = (int)(n % (uint32_t)base);
        buf[i++] = (r < 10) ? (char)('0' + r) : (char)('a' + r - 10);
        n /= (uint32_t)base;
    }

    while (i > 0) vga_putchar(buf[--i]);
}

void vga_printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putchar(*fmt++);
            continue;
        }

        fmt++;
        switch (*fmt) {
            case 's':
                vga_puts(__builtin_va_arg(args, const char *));
                break;
            case 'c':
                vga_putchar((char)__builtin_va_arg(args, int));
                break;
            case 'd': {
                int value = __builtin_va_arg(args, int);
                if (value < 0) {
                    vga_putchar('-');
                    value = -value;
                }
                print_uint((uint32_t)value, 10);
                break;
            }
            case 'u':
                print_uint(__builtin_va_arg(args, uint32_t), 10);
                break;
            case 'x':
                print_uint(__builtin_va_arg(args, uint32_t), 16);
                break;
            case '%':
                vga_putchar('%');
                break;
            default:
                vga_putchar(*fmt);
                break;
        }
        fmt++;
    }

    __builtin_va_end(args);
}

void vga_draw_box(
    int row,
    int col,
    int height,
    int width,
    vga_color_t color
)
{
    uint8_t saved_attr = cur_attr;

    int top = view_top + row;
    int bottom = top + height - 1;

    ensure_line_exists(bottom);

    cur_attr = VGA_ATTR(
        color,
        VGA_BLACK
    );

    /* Top + bottom border */
    for (int c = 0; c < width; c++)
    {
        if (col + c >= VGA_COLS)
            break;

        if (top >= first_line &&
            top <= last_line())
        {
            text_buffer[line_slot(top)][col + c] =
                (uint16_t)(
                    ((uint16_t)cur_attr << 8) |
                    (c == 0 ? 0xC9 :
                     c == width - 1 ? 0xBB : 0xCD)
                );
        }

        if (bottom >= first_line &&
            bottom <= last_line())
        {
            text_buffer[line_slot(bottom)][col + c] =
                (uint16_t)(
                    ((uint16_t)cur_attr << 8) |
                    (c == 0 ? 0xC8 :
                     c == width - 1 ? 0xBC : 0xCD)
                );
        }
    }

    /* Left + right border */
    for (int r = 1; r < height - 1; r++)
    {
        int line = top + r;

        if (line < first_line ||
            line > last_line())
            continue;

        if (col >= 0 && col < VGA_COLS)
        {
            text_buffer[line_slot(line)][col] =
                (uint16_t)(
                    ((uint16_t)cur_attr << 8) |
                    0xBA
                );
        }

        if (col + width - 1 >= 0 &&
            col + width - 1 < VGA_COLS)
        {
            text_buffer[line_slot(line)]
                       [col + width - 1] =
                (uint16_t)(
                    ((uint16_t)cur_attr << 8) |
                    0xBA
                );
        }
    }

    cur_attr = saved_attr;

    render_view();
}
