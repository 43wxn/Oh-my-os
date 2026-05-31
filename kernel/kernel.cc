/* ============================================================================
 * kernel/kernel.cc — 内核主函数 (M2: 中断系统)
 * ============================================================================ */

#include "kernel/printk.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/pit.h"
#include "kernel/arch/x86/port.h"
#include "kernel/drivers/keyboard/keyboard.h"

extern "C" void kernel_main() {
    /* 清屏 */
    uint16_t *vga = reinterpret_cast<uint16_t *>(0xB8000);
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }

    printk("+------------------------------------------+\n");
    printk("|  Oh-my-os Kernel v0.0.2 (M2)             |\n");
    printk("+------------------------------------------+\n");
    printk("\n");

    /* 1. 初始化中断描述符表 */
    printk("[INIT] Setting up IDT...\n");
    idt_init();
    printk("[INIT] IDT: 256 gates installed\n");

    /* 2. 初始化 PIC (重映射 IRQ0-15 → 0x20-0x2F) */
    printk("[INIT] Initializing PIC...\n");
    pic_init();
    printk("[INIT] PIC: IRQ remapped to 0x20-0x2F\n");

    /* 3. 初始化 PIT (100Hz 时钟中断) */
    pit_init(100);

    /* 4. 初始化键盘 */
    keyboard_init();

    /* 5. 诊断: 读 PIC 内部寄存器, 检查硬件中断是否到达 */
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0" : "=r"(eflags));
    printk("[INIT] EFLAGS before STI: 0x%x\n", eflags);

    uint8_t m1 = inb(0x21), m2 = inb(0xA1);
    printk("[INIT] PIC IMR: 0x%x / 0x%x\n", m1, m2);

    /* 读 PIC IRR (Interrupt Request Register) — 哪些 IRQ 在等待 */
    outb(0x20, 0x0A);
    uint8_t irr1 = inb(0x20);
    outb(0xA0, 0x0A);
    uint8_t irr2 = inb(0xA0);
    printk("[INIT] PIC IRR: 0x%x / 0x%x\n", irr1, irr2);

    /* 6. 开启中断 */
    printk("[INIT] Enabling STI...\n");
    __asm__ volatile ("sti");

    /* 7. 软件中断测试: 手动触发 int $0x20 (模拟 PIT IRQ0)
     *    验证 IDT → isr_stub → irq_common → irq_handler 链路 */
    printk("[TEST] Triggering software IRQ0 (int $0x20)...\n");
    __asm__ volatile ("int $0x20");
    printk("[TEST] After int $0x20: jiffies=%d (expect 1)\n", jiffies);

    /* 8. 软件中断测试: 手动触发 int $0x21 (模拟键盘 IRQ1) */
    printk("[TEST] Triggering software IRQ1 (int $0x21)...\n");
    __asm__ volatile ("int $0x21");
    printk("[TEST] After int $0x21: keyboard IRQ triggered\n");

    /* 9. 忙等轮询: 不用 hlt, 防止 CPU 永久休眠 */
    printk("\nM2: waiting for hardware interrupts...\n\n");

    uint32_t last_tick = 0;
    uint32_t loop_count = 0;

    while (1) {
        uint32_t cur = jiffies;
        if (cur != last_tick) {
            last_tick = cur;
            loop_count = 0;
            if ((cur % 100) == 0) {
                printk("[TICK] jiffies=%d\n", cur);
            }
        }

        loop_count++;
        /* 每 ~1 亿次循环打印诊断 (~5 秒 @1.6GHz) */
        if (loop_count >= 100000000) {
            outb(0x20, 0x0A);
            uint8_t irr = inb(0x20);
            uint8_t isr_val = 0;
            outb(0x20, 0x0B);
            isr_val = inb(0x20);
            printk("[DIAG] jiffies=%d PIC IRR=0x%x ISR=0x%x IMR=0x%x\n",
                   jiffies, irr, isr_val, inb(0x21));
            loop_count = 0;
        }
    }
}
