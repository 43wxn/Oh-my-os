/* ============================================================================
 * kernel/arch/x86/pic.cc — 8259A PIC 初始化
 *
 * 重映射原理:
 *   默认: IRQ0-7 → 0x08-0x0F, IRQ8-15 → 0x70-0x77
 *   问题: 0x08-0x0F 覆盖了 CPU 异常 #8-#15 (Double Fault ~ Reserved)
 *   解决: 重映射 IRQ0-7 → 0x20-0x27, IRQ8-15 → 0x28-0x2F
 *
 * VGA 调试标记 (row 24 屏幕底部):
 *   '1'=ICW1 '2'=ICW2 '3'=ICW3 '4'=ICW4 '5'=MASK 'X'=DONE
 *   红底 = 该步骤即将执行, 绿底 = 该步骤执行完毕
 * ============================================================================ */

#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/port.h"

#define DBG_ROW  24                    /* VGA row 24 (bottom)           */
#define DBG_RED  0x4F                  /* 红底白字 = 危险(未完成)       */
#define DBG_GRN  0x2F                  /* 绿底白字 = 安全(已完成)       */

static inline void vga_put(int col, char c, uint8_t attr) {
    uint16_t *vga = (uint16_t *)0xB8000;
    vga[DBG_ROW * 80 + col] = (uint16_t)((attr << 8) | c);
}

void pic_init() {
    /* ── ICW1: 开始初始化 ── */
    vga_put(0, '1', DBG_RED);          /* 标记: ICW1 开始               */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    vga_put(0, '1', DBG_GRN);          /* 标记: ICW1 完成               */

    /* ── ICW2: 中断向量偏移 ── */
    vga_put(2, '2', DBG_RED);
    outb(PIC1_DATA, PIC1_OFFSET);
    outb(PIC2_DATA, PIC2_OFFSET);
    vga_put(2, '2', DBG_GRN);

    /* ── ICW3: 级联配置 ── */
    vga_put(4, '3', DBG_RED);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    vga_put(4, '3', DBG_GRN);

    /* ── ICW4: 8086/88 模式 ── */
    vga_put(6, '4', DBG_RED);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    vga_put(6, '4', DBG_GRN);

    /* ── 屏蔽所有 IRQ ── */
    vga_put(8, '5', DBG_RED);
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    vga_put(8, '5', DBG_GRN);

    vga_put(10, 'X', DBG_GRN);         /* 标记: 全部完成                */
}

/* 发送 EOI (End Of Interrupt) */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);    /* 从片也需 EOI       */
    }
    outb(PIC1_CMD, PIC_EOI);        /* 主片总是需要 EOI   */
}

/* 屏蔽指定 IRQ */
void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/* 取消屏蔽指定 IRQ */
void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
