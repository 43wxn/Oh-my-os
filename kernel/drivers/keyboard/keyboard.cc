/* ============================================================================
 * kernel/drivers/keyboard/keyboard.cc — PS/2 键盘驱动
 *
 * 硬件: PS/2 控制器 (i8042), 端口 0x60/0x64, IRQ1
 *
 * 扫描码集: i8042 默认开启翻译 (Translation), 键盘发出的 Set 2
 *           被控制器翻译为 Set 1 (XT) 后交给 CPU.
 *           Set 1: Make = code (bit7=0), Break = code | 0x80 (bit7=1)
 *
 * 扩展码 (E0 前缀): 方向键 → 控制台滚动回溯.
 * ============================================================================ */

#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/arch/x86/port.h"
#include "kernel/arch/x86/isr.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/printk.h"

/* ── 环形缓冲区 ── */
#define KBD_BUF_SIZE  256

static char  kbd_buf[KBD_BUF_SIZE];
static int   kbd_head = 0;
static int   kbd_tail = 0;
static int   kbd_count = 0;

/* 修饰键状态 */
static bool  shift_l      = false;
static bool  shift_r      = false;
static bool  caps_lock    = false;
static bool  e0_prefix    = false;   /* 收到 E0 扩展前缀 */

/* ── Set 1 (XT) 扫描码 → ASCII (无 Shift) ── */
static const char scancode_ascii_lower[] = {
    0,    0,    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
/*  0x00  0x01  0x02  0x03  0x04  0x05  0x06  0x07  0x08  0x09  0x0A  0x0B  0x0C  0x0D  0x0E  */
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
/*  0x0F  0x10  0x11  0x12  0x13  0x14  0x15  0x16  0x17  0x18  0x19  0x1A  0x1B  0x1C  */
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
/*  0x1D  0x1E  0x1F  0x20  0x21  0x22  0x23  0x24  0x25  0x26  0x27  0x28  0x29  */
    0,    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
/*  0x2A  0x2B  0x2C  0x2D  0x2E  0x2F  0x30  0x31  0x32  0x33  0x34  0x35  0x36  */
    '*',  0,   ' ', 0,
/*  0x37  0x38  0x39  0x3A  */
};

/* 大写/Shift 版本 */
static const char scancode_ascii_upper[] = {
    0,    0,    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ', 0,
};

/* ── Set 1 修饰键扫描码 ── */
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPS     0x3A

/* ── 辅助函数 ── */

static void kbd_enqueue(char c) {
    if (kbd_count < KBD_BUF_SIZE) {
        kbd_buf[kbd_tail] = c;
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        kbd_count++;
    }
}

char kbd_getchar() {
    while (kbd_count == 0) {
        __asm__ volatile ("pause");
    }
    char c = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
    kbd_count--;
    return c;
}

int kbd_haschar() {
    return kbd_count > 0;
}

/* ── 扫描码诊断显示 (第 2 行, 显示最后 10 个 scancode) ── */
static int  sc_diag_pos = 0;
static void sc_diag(uint8_t code, char prefix) {
    uint16_t *v = (uint16_t *)0xB8000 + 80 * 1;  /* row 1 */
    int p = sc_diag_pos % 40;
    char hex[] = "0123456789ABCDEF";
    v[p*2]   = (0x0E << 8) | prefix;
    v[p*2+1] = (0x0E << 8) | hex[code >> 4];
    v[p*2+2] = (0x0E << 8) | hex[code & 0xF];
    sc_diag_pos++;
}

/* ── IRQ1 中断处理 ── */

static void kbd_irq_handler(int_frame_t *) {
    uint8_t scancode = inb(0x60);

    /* ── Set 1 Break Code: bit 7 = 1 ── */
    if (scancode & 0x80) {
        sc_diag(scancode, 'B');            /* B = Break */
        e0_prefix = false;
        uint8_t make = scancode & 0x7F;
        switch (make) {
        case SC_LSHIFT: shift_l = false; break;
        case SC_RSHIFT: shift_r = false; break;
        }
        return;
    }

    /* ── E0 扩展码前缀 ── */
    if (scancode == 0xE0) {
        sc_diag(scancode, 'E');            /* E = E0 prefix */
        e0_prefix = true;
        return;
    }

    /* ── 修饰键 Make ── */
    switch (scancode) {
    case SC_LSHIFT: shift_l = true;  sc_diag(scancode, 'S'); return;
    case SC_RSHIFT: shift_r = true;  sc_diag(scancode, 'S'); return;
    case SC_CAPS:   caps_lock = !caps_lock; sc_diag(scancode, 'C'); return;
    }

    /* ── E0 扩展键: 方向键 → 滚动回溯 ── */
    if (e0_prefix) {
        e0_prefix = false;
        sc_diag(scancode, 'X');            /* X = eXtended */
        switch (scancode) {
        case 0x48: console_scroll_up(1);     return;
        case 0x50: console_scroll_down(1);   return;
        case 0x49: console_scroll_up(25);    return;
        case 0x51: console_scroll_down(25);  return;
        default:   return;
        }
    }

    /* ── 普通键 ── */
    sc_diag(scancode, ' ');                /* 普通键 */

    /* ── 普通键: 扫描码 → ASCII ── */
    if (scancode >= sizeof(scancode_ascii_lower))
        return;

    bool shifted = shift_l || shift_r;
    bool use_upper = (shifted != caps_lock);
    char ascii = use_upper
        ? scancode_ascii_upper[scancode]
        : scancode_ascii_lower[scancode];

    if (ascii == 0)
        return;

    /* 如果在滚动模式, 任意字符键退出滚动 */
    if (console_is_scrolling()) {
        console_scroll_reset();
        return;  /* 按键被"吃掉", 不输出 */
    }

    kbd_enqueue(ascii);
    putchar(ascii);  /* 回显 */
}

/* ── 初始化 ── */

void keyboard_init() {
    irq_register(1, kbd_irq_handler);
    pic_clear_mask(1);           /* 取消 IRQ1 屏蔽 */

    printk("[KBD] PS/2 keyboard initialized (IRQ1)\n");
}
