/* ============================================================================
 * kernel/arch/x86/idt.cc — IDT 初始化
 *
 * 设置 256 个中断描述符并加载 IDTR。
 * 前 32 个 = CPU 异常, 33-47 = IRQ (PIC 重映射后), 0x80 = 系统调用。
 * ============================================================================ */

#include "kernel/arch/x86/idt.h"

/* 汇编定义的中断桩 (见 isr_stubs.S) */
extern "C" {
    /* CPU 异常桩 (0-31) */
    extern void isr0();
    extern void isr1();
    extern void isr2();
    extern void isr3();
    extern void isr4();
    extern void isr5();
    extern void isr6();
    extern void isr7();
    extern void isr8();
    extern void isr9();
    extern void isr10();
    extern void isr11();
    extern void isr12();
    extern void isr13();
    extern void isr14();
    extern void isr15();
    extern void isr16();
    extern void isr17();
    extern void isr18();
    extern void isr19();
    extern void isr20();
    extern void isr21();
    extern void isr22();
    extern void isr23();
    extern void isr24();
    extern void isr25();
    extern void isr26();
    extern void isr27();
    extern void isr28();
    extern void isr29();
    extern void isr30();
    extern void isr31();

    /* IRQ 桩 (32-47) */
    extern void irq0();
    extern void irq1();
    extern void irq2();
    extern void irq3();
    extern void irq4();
    extern void irq5();
    extern void irq6();
    extern void irq7();
    extern void irq8();
    extern void irq9();
    extern void irq10();
    extern void irq11();
    extern void irq12();
    extern void irq13();
    extern void irq14();
    extern void irq15();

    /* 系统调用桩 (0x80) */
    extern void isr_syscall();
}

/* IDT 表 (256 个描述符) */
static idt_entry_t idt[IDT_ENTRIES];

/* IDTR */
static idtr_t idtr;

/* 设置一个 IDT 门 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = selector;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

/* 加载 IDTR */
static void idt_flush() {
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

void idt_init() {
    /* 设置 IDTR */
    idtr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idtr.base  = reinterpret_cast<uint32_t>(&idt[0]);

    /* 全部设为 0 (未使用的门) */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* CPU 异常 (0-31): DPL=0, 中断门
     * 注意: 这些是陷阱门 (不自动关中断), 但为了简单统一用中断门 */
    idt_set_gate(0,  reinterpret_cast<uint32_t>(isr0),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(1,  reinterpret_cast<uint32_t>(isr1),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(2,  reinterpret_cast<uint32_t>(isr2),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(3,  reinterpret_cast<uint32_t>(isr3),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(4,  reinterpret_cast<uint32_t>(isr4),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(5,  reinterpret_cast<uint32_t>(isr5),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(6,  reinterpret_cast<uint32_t>(isr6),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(7,  reinterpret_cast<uint32_t>(isr7),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(8,  reinterpret_cast<uint32_t>(isr8),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(9,  reinterpret_cast<uint32_t>(isr9),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(10, reinterpret_cast<uint32_t>(isr10), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(11, reinterpret_cast<uint32_t>(isr11), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(12, reinterpret_cast<uint32_t>(isr12), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(13, reinterpret_cast<uint32_t>(isr13), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(14, reinterpret_cast<uint32_t>(isr14), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(15, reinterpret_cast<uint32_t>(isr15), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(16, reinterpret_cast<uint32_t>(isr16), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(17, reinterpret_cast<uint32_t>(isr17), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(18, reinterpret_cast<uint32_t>(isr18), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(19, reinterpret_cast<uint32_t>(isr19), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(20, reinterpret_cast<uint32_t>(isr20), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(21, reinterpret_cast<uint32_t>(isr21), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(22, reinterpret_cast<uint32_t>(isr22), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(23, reinterpret_cast<uint32_t>(isr23), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(24, reinterpret_cast<uint32_t>(isr24), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(25, reinterpret_cast<uint32_t>(isr25), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(26, reinterpret_cast<uint32_t>(isr26), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(27, reinterpret_cast<uint32_t>(isr27), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(28, reinterpret_cast<uint32_t>(isr28), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(29, reinterpret_cast<uint32_t>(isr29), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(30, reinterpret_cast<uint32_t>(isr30), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(31, reinterpret_cast<uint32_t>(isr31), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);

    /* IRQ 0-15 (PIC 重映射后 → 0x20-0x2F) */
    idt_set_gate(32, reinterpret_cast<uint32_t>(irq0),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(33, reinterpret_cast<uint32_t>(irq1),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(34, reinterpret_cast<uint32_t>(irq2),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(35, reinterpret_cast<uint32_t>(irq3),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(36, reinterpret_cast<uint32_t>(irq4),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(37, reinterpret_cast<uint32_t>(irq5),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(38, reinterpret_cast<uint32_t>(irq6),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(39, reinterpret_cast<uint32_t>(irq7),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(40, reinterpret_cast<uint32_t>(irq8),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(41, reinterpret_cast<uint32_t>(irq9),  0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(42, reinterpret_cast<uint32_t>(irq10), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(43, reinterpret_cast<uint32_t>(irq11), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(44, reinterpret_cast<uint32_t>(irq12), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(45, reinterpret_cast<uint32_t>(irq13), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(46, reinterpret_cast<uint32_t>(irq14), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);
    idt_set_gate(47, reinterpret_cast<uint32_t>(irq15), 0x08, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INT_GATE);

    /* 系统调用 (int 0x80): DPL=3, 陷阱门 (允许用户态调用) */
    idt_set_gate(0x80, reinterpret_cast<uint32_t>(isr_syscall), 0x08,
                 IDT_FLAG_PRESENT | IDT_FLAG_DPL3 | IDT_FLAG_TRAP_GATE);

    /* 加载 IDTR */
    idt_flush();
}
