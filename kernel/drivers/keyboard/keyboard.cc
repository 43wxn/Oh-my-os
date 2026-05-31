/* ============================================================================
 * kernel/drivers/keyboard/keyboard.cc — PS/2 键盘驱动
 *
 * 硬件: PS/2 控制器 (i8042), AT Set 2 扫描码
 * 端口: 0x60 数据, 0x64 状态/命令
 * IRQ:  1 → PIC 重映射后为 0x21 → 中断向量 33
 *
 * 当前只处理按下 (Make Code), 忽略松开 (Break Code)。
 * 支持 Shift 切换大小写和符号。
 * ============================================================================ */

#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/arch/x86/port.h"
#include "kernel/arch/x86/isr.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/printk.h"

/* ── 环形缓冲区 ── */
#define KBD_BUF_SIZE  256

static char  kbd_buf[KBD_BUF_SIZE];
static int   kbd_head = 0;         /* 读位置 */
static int   kbd_tail = 0;         /* 写位置 */
static int   kbd_count = 0;

/* 修饰键状态 */
static bool  shift_pressed = false;
static bool  caps_lock     = false;
static bool  expect_break  = false;  /* 收到 0xF0 后等 break code */

/* ── AT Set 2 扫描码 → ASCII (无 Shift) ── */
static const char scancode_ascii_lower[] = {
    0,    0,    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ', 0,
};

/* 对应的大写/Shift 版本 */
static const char scancode_ascii_upper[] = {
    0,    0,    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ', 0,
};

/* ── 辅助函数 ── */

/* 字符入队 */
static void kbd_enqueue(char c) {
    if (kbd_count < KBD_BUF_SIZE) {
        kbd_buf[kbd_tail] = c;
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        kbd_count++;
    }
}

/* 字符出队 */
char kbd_getchar() {
    while (kbd_count == 0) {
        /* 忙等待 — 后续用调度器替代 */
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

/* ── IRQ1 中断处理 ── */

static void kbd_irq_handler(int_frame_t *) {
    uint8_t scancode = inb(0x60);

    /* ── 状态机: 处理 AT Set 2 多字节序列 ── */
    if (expect_break) {
        /* 前一个字节是 0xF0, 这个是 break code */
        expect_break = false;
        switch (scancode) {
        case 0x12: case 0x59:  /* Shift 释放 */
            shift_pressed = false;
            return;
        }
        return;  /* 忽略其他键的释放 */
    }

    if (scancode == 0xF0) {
        expect_break = true;   /* 下一个字节是 break code */
        return;
    }

    if (scancode == 0xE0) {
        return;  /* 扩展码前缀, 当前忽略 (方向键等) */
    }

    /* ── 修饰键 Make ── */
    switch (scancode) {
    case 0x12: case 0x59:  /* Shift */
        shift_pressed = true;
        return;
    case 0x58:              /* Caps Lock (toggle) */
        caps_lock = !caps_lock;
        return;
    }

    /* ── 普通键: 扫描码 → ASCII ── */
    if (scancode >= sizeof(scancode_ascii_lower)) {
        return;
    }

    bool use_upper = (shift_pressed != caps_lock);
    char ascii = use_upper
        ? scancode_ascii_upper[scancode]
        : scancode_ascii_lower[scancode];

    if (ascii == 0) {
        return;
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
