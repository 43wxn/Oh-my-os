/* ============================================================================
 * kernel/arch/x86/pic.cc — 8259A PIC 初始化
 *
 * 重映射原理:
 *   默认: IRQ0-7 → 0x08-0x0F, IRQ8-15 → 0x70-0x77
 *   问题: 0x08-0x0F 覆盖了 CPU 异常 #8-#15 (Double Fault ~ Reserved)
 *   解决: 重映射 IRQ0-7 → 0x20-0x27, IRQ8-15 → 0x28-0x2F
 *
 * PIC 初始化需要严格的 I/O 延迟——实机比 QEMU 更需要 io_wait()
 * ============================================================================ */

#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/port.h"

void pic_init() {
    /* ── ICW1: 开始初始化 (边沿触发 + 级联 + ICW4) ── */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ── ICW2: 中断向量偏移 ── */
    outb(PIC1_DATA, PIC1_OFFSET);   /* 主片: IRQ0 → 0x20  */
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);   /* 从片: IRQ8 → 0x28  */
    io_wait();

    /* ── ICW3: 级联配置 ── */
    outb(PIC1_DATA, 0x04);          /* 主片: IRQ2 接从片   */
    io_wait();
    outb(PIC2_DATA, 0x02);          /* 从片: 接到主片 IRQ2 */
    io_wait();

    /* ── ICW4: 8086/88 模式 ── */
    outb(PIC1_DATA, 0x01);          /* 8086 模式, 非自动 EOI */
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* ── 屏蔽所有 IRQ, 等各驱动初始化时自行打开 ── */
    outb(PIC1_DATA, 0xFF);          /* 主片: 全部屏蔽 */
    outb(PIC2_DATA, 0xFF);          /* 从片: 全部屏蔽 */
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
