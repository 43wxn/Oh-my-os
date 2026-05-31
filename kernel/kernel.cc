/* ============================================================================
 * kernel/kernel.cc — 内核主函数 (M4: 进程管理)
 * ============================================================================ */

#include "kernel/printk.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/pit.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/arch/x86/port.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/mm/heap.h"
#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/proc/proc.h"

/* ── M4 测试线程 ── */
static void thread_a() {
    uint16_t *vga = (uint16_t *)0xB8000;
    while (1) {
        vga[80 * 12 + 30] = (0x0A << 8) | 'A';
        for (volatile int i = 0; i < 500000; i++) {}
    }
}

static void thread_b() {
    uint16_t *vga = (uint16_t *)0xB8000;
    while (1) {
        vga[80 * 13 + 30] = (0x0C << 8) | 'B';
        for (volatile int i = 0; i < 500000; i++) {}
    }
}

extern "C" void kernel_main() {
    /* 清屏 */
    uint16_t *vga = reinterpret_cast<uint16_t *>(0xB8000);
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }

    printk("+------------------------------------------+\n");
    printk("|  Oh-my-os Kernel v0.0.4 (M4)             |\n");
    printk("+------------------------------------------+\n\n");

    /* ── 中断系统 (M2) ── */
    printk("[1] IDT...\n");
    idt_init();
    printk("[1] IDT OK\n");

    printk("[2] PIC...\n");
    pic_init();
    printk("[2] PIC OK\n");

    /* ── M3: 内存管理 ── */
    printk("[3a] PMM init...\n");
    pmm_init();
    printk("[3a] PMM OK\n");

    printk("[3b] VMM init...\n");
    vmm_init();
    printk("[3b] VMM OK, paging=%d\n", paging_is_enabled());

    printk("[3c] Heap init...\n");
    heap_init();
    printk("[3c] Heap OK\n");

    /* ── M4: 进程管理 (Step 1: cooperative yield) ── */
    printk("[4a] Proc init...\n");
    proc_init();

    static pcb boot_pcb;
    proc_set_current(&boot_pcb);

    printk("[4b] Creating threads...\n");
    proc_create(thread_a);
    proc_create(thread_b);
    printk("[4b] Threads created\n");

    /* Yield to let threads run */
    vga[80 * 0 + 70] = (0x4F << 8) | '>';

    while (1) {
        proc_yield();
    }
}
