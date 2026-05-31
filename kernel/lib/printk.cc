/* ============================================================================
 * kernel/lib/printk.cc — 内核格式化输出 + 滚动回溯缓冲区
 *
 * VGA 文本模式 (80x25), 直接写 0xB8000.
 * 支持格式: %s %d %x %c
 *
 * 滚动回溯:
 *   当输出超过 25 行时, 被滚出屏幕的每一行存入环形缓冲区.
 *   PageUp/PageDown 方向键可回溯查看, 按任意字符键退出.
 * ============================================================================ */

#include "kernel/printk.h"
#include "kernel/arch/x86/port.h"
#include <stdint.h>

/* ── 光标位置 ── */
static int cursor_row = 0;
static int cursor_col = 0;

/* ── 滚动回溯缓冲区 ── */
#define SB_LINES   500               /* 最多保存 500 行 */
static uint16_t sb_buf[SB_LINES][VGA_WIDTH];
static int      sb_write = 0;        /* 下一次写入的行索引   */
static int      sb_count = 0;        /* 已保存的总行数       */
static int      scroll_off = 0;      /* 回溯偏移 (0=实时)    */

/* ── 内部: 渲染回溯视图 ── */
static void console_render() {
    uint16_t *vga = VGA_MEMORY;

    if (scroll_off == 0) return;  /* 实时模式, 不干预 */

    /* 从缓冲区读取并显示 */
    for (int r = 0; r < VGA_HEIGHT; r++) {
        /* 计算缓冲区索引: 最新的行是 sb_write-1, 往前 scroll_off 行 */
        int idx = sb_write - scroll_off + (VGA_HEIGHT - 1 - r);
        /* 修正负数索引 (环形) */
        while (idx < 0) idx += SB_LINES;
        idx %= SB_LINES;

        if (r < sb_count - scroll_off) {
            /* 从缓冲区复制 */
            for (int c = 0; c < VGA_WIDTH; c++) {
                vga[r * VGA_WIDTH + c] = sb_buf[idx][c];
            }
        } else {
            /* 超出缓冲区范围, 显示空行 */
            for (int c = 0; c < VGA_WIDTH; c++) {
                vga[r * VGA_WIDTH + c] = 0x0F20;
            }
        }
    }

    /* 底部状态栏 */
    uint16_t *bar = &vga[(VGA_HEIGHT - 1) * VGA_WIDTH];
    const char *msg = " [SCROLL] Up/Dn:line  PgUp/PgDn:page  Any key:exit ";
    for (int i = 0; i < VGA_WIDTH && msg[i]; i++) {
        bar[i] = (uint16_t)msg[i] | ((uint16_t)0x70 << 8);  /* 灰底黑字 */
    }
}

/* ── 内部: 把一行存入缓冲区 ── */
static void sb_save_line(int row) {
    uint16_t *vga = VGA_MEMORY;
    for (int c = 0; c < VGA_WIDTH; c++) {
        sb_buf[sb_write][c] = vga[row * VGA_WIDTH + c];
    }
    sb_write = (sb_write + 1) % SB_LINES;
    if (sb_count < SB_LINES) sb_count++;
}

/* ── 公共 API ── */

void console_scroll_up(int lines) {
    /* 首次进入滚动: 保存当前 VGA 画面到缓冲区 */
    if (scroll_off == 0) {
        uint16_t *vga = VGA_MEMORY;
        for (int r = 0; r < VGA_HEIGHT; r++) {
            for (int c = 0; c < VGA_WIDTH; c++) {
                sb_buf[sb_write][c] = vga[r * VGA_WIDTH + c];
            }
            sb_write = (sb_write + 1) % SB_LINES;
            if (sb_count < SB_LINES) sb_count++;
        }
    }
    if (scroll_off + lines <= sb_count) {
        scroll_off += lines;
        console_render();
    }
}

void console_scroll_down(int lines) {
    if (scroll_off > lines) {
        scroll_off -= lines;
        console_render();
    } else if (scroll_off > 0) {
        scroll_off = 0;
        /* 退出滚动模式: 恢复实时画面 */
        /* 注意: 无法完全恢复, 只能清屏后重新渲染 */
        for (int r = 0; r < VGA_HEIGHT; r++) {
            uint16_t *vga = VGA_MEMORY;
            for (int c = 0; c < VGA_WIDTH; c++) {
                vga[r * VGA_WIDTH + c] = 0x0F20;
            }
        }
        /* 简单起见: 实时模式不恢复历史, 后续 printk 自然会更新 */
    }
}

void console_scroll_reset() {
    scroll_off = 0;
    /* 恢复实时画面: 显示最后 VGA_HEIGHT 行缓冲区 */
    uint16_t *vga = VGA_MEMORY;
    for (int r = 0; r < VGA_HEIGHT; r++) {
        int idx = sb_write - VGA_HEIGHT + r;
        while (idx < 0) idx += SB_LINES;
        idx %= SB_LINES;
        if ((sb_count >= VGA_HEIGHT && r < VGA_HEIGHT) ||
            (sb_count < VGA_HEIGHT && r >= VGA_HEIGHT - sb_count)) {
            for (int c = 0; c < VGA_WIDTH; c++)
                vga[r * VGA_WIDTH + c] = sb_buf[idx][c];
        } else {
            for (int c = 0; c < VGA_WIDTH; c++)
                vga[r * VGA_WIDTH + c] = 0x0F20;
        }
    }
}

int console_is_scrolling() {
    return scroll_off > 0;
}

/* ── 在滚动模式下模拟输出 (不写 VGA, 只更新光标状态) ── */
static void putchar_shadow(char c, int *row, int *col) {
    switch (c) {
    case '\n': (*row)++; *col = 0; break;
    case '\r': *col = 0; break;
    case '\t': *col = (*col + 4) & ~3;
               if (*col >= VGA_WIDTH) { *col = 0; (*row)++; }
               break;
    default:   (*col)++;
               if (*col >= VGA_WIDTH) { *col = 0; (*row)++; }
               break;
    }
}

/* ── 输出单个字符到 VGA ── */
void putchar(char c) {
    uint16_t *vga = VGA_MEMORY;

    /* 滚动模式: 不写 VGA, 只跟踪虚拟光标 */
    if (scroll_off > 0) {
        putchar_shadow(c, &cursor_row, &cursor_col);
        if (cursor_row >= VGA_HEIGHT) {
            /* 虚拟滚屏: 无需保存 VGA (已离开实时画面) */
            cursor_row = VGA_HEIGHT - 1;
        }
        return;
    }

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
            (uint16_t)c | ((uint16_t)0x0F << 8);
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
        break;
    }

    /* 滚屏: 保存滚出的行到回溯缓冲区 */
    if (cursor_row >= VGA_HEIGHT) {
        sb_save_line(0);
        for (int r = 0; r < VGA_HEIGHT - 1; r++) {
            for (int c2 = 0; c2 < VGA_WIDTH; c2++) {
                vga[r * VGA_WIDTH + c2] = vga[(r + 1) * VGA_WIDTH + c2];
            }
        }
        for (int c2 = 0; c2 < VGA_WIDTH; c2++) {
            vga[(VGA_HEIGHT - 1) * VGA_WIDTH + c2] = 0x0F00;
        }
        cursor_row = VGA_HEIGHT - 1;
    }
}

/* ── 清屏 ── */
void clear_screen() {
    uint16_t *vga = VGA_MEMORY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = 0x0F20;
    }
    cursor_row = 0;
    cursor_col = 0;
    scroll_off = 0;
}

/* ── 输出十进制整数 ── */
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

/* ── 输出十六进制整数 ── */
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

/* ── printk ── */
void printk(const char *fmt, ...) {
    uint32_t *args = (uint32_t *)(&fmt) + 1;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case '%': putchar('%'); break;
            case 's': {
                const char *s = (const char *)*args++;
                while (*s) putchar(*s++);
                break;
            }
            case 'd': print_int((int)*args++); break;
            case 'x': print_hex(*args++); break;
            case 'c': putchar((char)*args++); break;
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
