/* ============================================================================
 * kernel/lib/printk.cc — 内核格式化输出
 *
 * 直接写 VGA 显存 (0xB8000)。
 * 当前支持: %s (字符串), %d (整数), %x (十六进制), %c (字符)
 * 后续迭代会逐步完善。
 * ============================================================================ */

#include "kernel/printk.h"

/* 光标位置 */
static int cursor_row = 0;
static int cursor_col = 0;

/* 输出单个字符到 VGA */
void putchar(char c) {
    uint16_t *vga = VGA_MEMORY;

    switch (c) {
    case '\n':
        cursor_row++;
        cursor_col = 0;
        break;
    case '\r':
        cursor_col = 0;
        break;
    case '\t':
        cursor_col = (cursor_col + 4) & ~3;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
        break;
    default:
        vga[cursor_row * VGA_WIDTH + cursor_col] =
            (uint16_t)c | ((uint16_t)0x0F << 8);  /* 白字黑底 */
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
        break;
    }

    /* 滚屏 */
    if (cursor_row >= VGA_HEIGHT) {
        /* 向上滚动一行 */
        for (int r = 0; r < VGA_HEIGHT - 1; r++) {
            for (int c = 0; c < VGA_WIDTH; c++) {
                vga[r * VGA_WIDTH + c] = vga[(r + 1) * VGA_WIDTH + c];
            }
        }
        /* 清空最后一行 */
        for (int c = 0; c < VGA_WIDTH; c++) {
            vga[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = 0x0F00;
        }
        cursor_row = VGA_HEIGHT - 1;
    }
}

/* 清屏 */
void clear_screen() {
    uint16_t *vga = VGA_MEMORY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = 0x0F20;  /* space (0x20) + white-on-black (0x0F) */
    }
    cursor_row = 0;
    cursor_col = 0;
}

/* 输出十进制整数 */
static void print_int(int n) {
    if (n < 0) {
        putchar('-');
        n = -n;
    }
    if (n == 0) {
        putchar('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        putchar(buf[--i]);
    }
}

/* 输出十六进制整数 */
static void print_hex(uint32_t n) {
    putchar('0');
    putchar('x');
    if (n == 0) {
        putchar('0');
        return;
    }
    char buf[8];
    int i = 0;
    while (n > 0) {
        int digit = n & 0xF;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        n >>= 4;
    }
    while (i > 0) {
        putchar(buf[--i]);
    }
}

/* printk — 精简的格式化输出 */
void printk(const char *fmt, ...) {
    /* 获取可变参数 (i386 cdecl: 参数都在栈上) */
    uint32_t *args = (uint32_t *)(&fmt) + 1;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case '%':
                putchar('%');
                break;
            case 's': {
                const char *s = (const char *)*args++;
                while (*s) putchar(*s++);
                break;
            }
            case 'd':
                print_int((int)*args++);
                break;
            case 'x':
                print_hex(*args++);
                break;
            case 'c':
                putchar((char)*args++);
                break;
            default:
                putchar('%');
                putchar(*fmt);
                break;
            }
        } else {
            putchar(*fmt);
        }
        fmt++;
    }
}
