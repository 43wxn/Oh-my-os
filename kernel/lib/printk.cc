/* ============================================================================
 * kernel/lib/printk.cc — 内核格式化输出 + 滚动回溯
 *
 * VGA 文本模式 (80x25), 内存映射在 0xB8000.
 * 支持格式: %s %d %x %c
 *
 * 滚动回溯: 环形缓冲区保存所有输出行.
 *   PageUp/Down → 翻页,   Up/Down → 单行
 *   任意字符键 → 退出滚动
 * ============================================================================ */

#include "kernel/printk.h"
#include <stdint.h>

/* ── 光标位置 ── */
static int cursor_row = 0;
static int cursor_col = 0;

/* ── 滚动回溯环形缓冲区 ── */
#define SB_MAX   1000
static uint16_t sb_buf[SB_MAX][VGA_WIDTH];  /* 每行 80 个 uint16_t */
static int      sb_head = 0;   /* 最早的行 (读指针) */
static int      sb_tail = 0;   /* 下一个写入位置       */
static int      sb_count = 0;  /* 缓冲区中的行数       */
static int      scroll_off = 0; /* >0 = 滚动模式, 偏移行数 */

/* ── 加一行到缓冲区 ── */
static void sb_put(const uint16_t *line) {
    for (int c = 0; c < VGA_WIDTH; c++)
        sb_buf[sb_tail][c] = line[c];
    sb_tail = (sb_tail + 1) % SB_MAX;
    if (sb_count < SB_MAX)
        sb_count++;
    else
        sb_head = (sb_head + 1) % SB_MAX;  /* 最老的被覆盖 */
}

/* ── 读缓冲区中第 n 行 (0 = 最新) ── */
static uint16_t* sb_get(int n) {
    int idx = sb_tail - 1 - n;
    while (idx < 0) idx += SB_MAX;
    return sb_buf[idx % SB_MAX];
}

/* ── 渲染 VGA (实时或滚动) ── */
static void vga_render() {
    uint16_t *vga = VGA_MEMORY;

    if (scroll_off == 0) {
        /* 实时模式: 不干预 VGA */
        return;
    }

    /* 滚动模式: 用缓冲区覆盖整个屏幕 */
    int start = scroll_off + VGA_HEIGHT - 1;  /* 显示的最老行 */
    for (int r = 0; r < VGA_HEIGHT; r++) {
        int n = start - r;  /* 第 n 行 (0=最新) */
        uint16_t *src;
        if (n >= 0 && n < sb_count) {
            src = sb_get(n);
        } else {
            src = nullptr;
        }
        for (int c = 0; c < VGA_WIDTH; c++) {
            vga[r * VGA_WIDTH + c] = src ? src[c] : ((uint16_t)0x0F20);
        }
    }

    /* 底部状态栏 */
    const char *bar = "  SCROLL  Up/Dn PgUp/PgDn  AnyKey=exit  ";
    for (int i = 0; bar[i]; i++)
        vga[(VGA_HEIGHT-1)*VGA_WIDTH + i] = (0x70 << 8) | bar[i];
}

/* ══════════════════════════════════════════════════════════════
 * 公共 API
 * ══════════════════════════════════════════════════════════════ */

void console_scroll_up(int lines) {
    if (scroll_off + lines <= sb_count)
        scroll_off += lines;
    vga_render();
}

void console_scroll_down(int lines) {
    if (lines >= scroll_off)
        scroll_off = 0;
    else
        scroll_off -= lines;
    vga_render();
}

void console_scroll_reset() {
    scroll_off = 0;
    vga_render();
}

int console_is_scrolling() {
    return scroll_off > 0;
}

/* ══════════════════════════════════════════════════════════════
 * 输出
 * ══════════════════════════════════════════════════════════════ */

void putchar(char c) {
    uint16_t *vga = VGA_MEMORY;

    /* 滚动模式下只更新虚拟光标, 不写 VGA */
    if (scroll_off > 0) {
        if (c == '\n') { cursor_row++; cursor_col = 0; }
        else if (c == '\r') { cursor_col = 0; }
        else if (c == '\t') {
            cursor_col = (cursor_col + 4) & ~3;
            if (cursor_col >= VGA_WIDTH) { cursor_col = 0; cursor_row++; }
        } else {
            cursor_col++;
            if (cursor_col >= VGA_WIDTH) { cursor_col = 0; cursor_row++; }
        }
        if (cursor_row >= VGA_HEIGHT) cursor_row = VGA_HEIGHT - 1;
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
        if (cursor_col >= VGA_WIDTH) { cursor_col = 0; cursor_row++; }
        break;
    default:
        vga[cursor_row * VGA_WIDTH + cursor_col] =
            (uint16_t)c | ((uint16_t)0x0F << 8);
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) { cursor_col = 0; cursor_row++; }
        break;
    }

    /* 滚屏: 最上面一行存入缓冲区 */
    while (cursor_row >= VGA_HEIGHT) {
        sb_put(&vga[0]);                     /* 保存第 0 行    */
        for (int r = 0; r < VGA_HEIGHT - 1; r++)
            for (int c2 = 0; c2 < VGA_WIDTH; c2++)
                vga[r * VGA_WIDTH + c2] = vga[(r+1) * VGA_WIDTH + c2];
        for (int c2 = 0; c2 < VGA_WIDTH; c2++)
            vga[(VGA_HEIGHT-1) * VGA_WIDTH + c2] = 0x0F00;
        cursor_row--;
    }
}

void putbackspace() {
    /* 如果在滚动模式, 不处理 */
    if (scroll_off > 0) return;

    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        /* 行首退格: 回到上一行末尾 */
        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    } else {
        return;  /* 已经在 (0,0), 无法退格 */
    }

    /* 写空格擦除当前光标位置的字符 */
    uint16_t *vga = VGA_MEMORY;
    vga[cursor_row * VGA_WIDTH + cursor_col] = 0x0F20;
}

void clear_screen() {
    uint16_t *vga = VGA_MEMORY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = 0x0F20;
    cursor_row = 0;
    cursor_col = 0;
    scroll_off = 0;
}

/* ── print_int / print_hex / printk ── */

static void print_int(int n) {
    if (n < 0) { putchar('-'); n = -n; }
    if (n == 0) { putchar('0'); return; }
    char buf[12]; int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) putchar(buf[--i]);
}

static void print_hex(uint32_t n) {
    putchar('0'); putchar('x');
    if (n == 0) { putchar('0'); return; }
    char buf[8]; int i = 0;
    while (n > 0) {
        int d = n & 0xF;
        buf[i++] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        n >>= 4;
    }
    while (i > 0) putchar(buf[--i]);
}

void printk(const char *fmt, ...) {
    uint32_t *args = (uint32_t *)(&fmt) + 1;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case '%': putchar('%'); break;
            case 's': { const char *s = (const char *)*args++; while (*s) putchar(*s++); break; }
            case 'd': print_int((int)*args++); break;
            case 'x': print_hex(*args++); break;
            case 'c': putchar((char)*args++); break;
            default:  putchar('%'); putchar(*fmt); break;
            }
        } else {
            putchar(*fmt);
        }
        fmt++;
    }
}
