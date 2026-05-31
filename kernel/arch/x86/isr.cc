/* ============================================================================
 * kernel/arch/x86/isr.cc — 中断/异常分发
 *
 * isr_handler() 处理 CPU 异常 (0-31)。
 * irq_handler() 分发硬件 IRQ 到已注册的处理函数。
 * ============================================================================ */

#include "kernel/arch/x86/isr.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/printk.h"

/* 异常名称表 */
static const char *exception_names[] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "Reserved",
    "#MF x87 FPU Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD FPU",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "#SX Security Exception",
    "Reserved",
};

/* IRQ 处理函数表 (16 个硬件中断) */
static irq_handler_t irq_handlers[16];

/* 注册 IRQ 处理函数 */
void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/* CPU 异常处理 */
extern "C" void isr_handler(int_frame_t *frame) {
    /* 当前阶段: 异常时打印信息并停机。
     * M3(内存管理) 之后, #PF 缺页异常将用于按需分页。 */

    printk("\n!! EXCEPTION: ");
    if (frame->int_no < 32) {
        printk(exception_names[frame->int_no]);
    } else {
        printk("Unknown (int ");
        printk("%d", frame->int_no);
        printk(")");
    }
    printk("\n");
    printk("   EIP: ");
    printk("%x", frame->eip);
    printk("  CS: ");
    printk("%x", frame->cs);
    printk("  EFLAGS: ");
    printk("%x", frame->eflags);
    printk("\n");
    if (frame->int_no == 14) {
        /* #PF 缺页: 打印 CR2 (导致缺页的地址) */
        uint32_t cr2;
        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        printk("   Fault address (CR2): ");
        printk("%x", cr2);
        printk("\n");
    }
    printk("\nSystem halted.\n");

    /* 停机 */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* 硬件中断分发 */
extern "C" void irq_handler(int_frame_t *frame) {
    /* frame->int_no 范围: 32-47 (PIC 重映射后) */
    uint8_t irq = frame->int_no - 32;

    /* 发送 EOI */
    pic_send_eoi(irq);

    /* 调用注册的处理函数 */
    if (irq < 16 && irq_handlers[irq] != nullptr) {
        irq_handlers[irq](frame);
    }
}
