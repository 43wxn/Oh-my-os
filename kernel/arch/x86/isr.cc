/* ============================================================================
 * kernel/arch/x86/isr.cc — 中断/异常分发
 *
 * isr_handler() 处理 CPU 异常 (0-31)。
 * irq_handler() 分发硬件 IRQ 到已注册的处理函数。
 *
 * #PF (INT 0x0E): 缺页异常 — Demand Paging
 *   检查 error_code bit 0: 0=页不存在 → 分配物理页 → 映射 → 重试
 *   其他异常一律打印信息并停机.
 * ============================================================================ */

#include "kernel/arch/x86/isr.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
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

    /* ── #PF (INT 0x0E): Demand Paging ── */
    if (frame->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));

        /* error_code bit 0 = 0 → 页不存在 (可修复) */
        if (!(frame->err_code & 1)) {
            uint32_t vaddr = cr2 & PAGE_MASK;   /* 页对齐 */

            /* 拒绝映射 NULL 页和 0 页 (程序员 bug) */
            if (vaddr >= PAGE_SIZE) {
                void *phys = pmm_alloc_page();
                if (phys) {
                    /* 默认权限: 内核 R/W, 用户态按 error_code bit2 */
                    uint32_t flags = PAGE_PRESENT | PAGE_RW;
                    if (frame->err_code & 4)   /* bit 2 = user-mode access */
                        flags |= PAGE_USER;

                    int rc = vmm_map_page(vaddr, (uint32_t)phys, flags);
                    if (rc == 0) {
                        return;  /* 映射成功, CPU 重试触发异常的指令 */
                    }
                    pmm_free_page(phys);  /* 映射失败, 回滚 */
                }
            }
        }
        /* 不可修复的 #PF: 打印详细信息后停机 */
    }

    /* ── 不可恢复的异常: 打印信息 + 停机 ── */
    printk("\n!! EXCEPTION: ");
    if (frame->int_no < 32) {
        printk(exception_names[frame->int_no]);
    } else {
        printk("Unknown (int %d)", frame->int_no);
    }
    printk("\n");
    printk("   EIP: %x  CS: %x  EFLAGS: %x\n",
           frame->eip, frame->cs, frame->eflags);
    if (frame->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        printk("   CR2: %x  err_code: %x\n", cr2, frame->err_code);
    }
    printk("System halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* 硬件中断分发 */
extern "C" void irq_handler(int_frame_t *frame) {
    uint8_t irq = frame->int_no - 32;

    pic_send_eoi(irq);

    if (irq < 16 && irq_handlers[irq] != nullptr) {
        irq_handlers[irq](frame);
    }
}
