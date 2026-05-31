/* ============================================================================
 * kernel/kernel.cc — 内核主函数 (M3: 内存管理)
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

extern "C" void kernel_main() {
    /* 清屏 */
    uint16_t *vga = reinterpret_cast<uint16_t *>(0xB8000);
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }

    printk("+------------------------------------------+\n");
    printk("|  Oh-my-os Kernel v0.0.3 (M3)             |\n");
    printk("+------------------------------------------+\n\n");

    /* ── 中断系统 (M2) ── */
    printk("[INIT] IDT... ");
    idt_init();
    printk("OK\n");

    printk("[INIT] PIC... ");
    pic_init();
    printk("OK\n");

    /* ── 内存管理 (M3) ── */
    printk("[INIT] Physical Memory Manager...\n");
    pmm_init();

    printk("[INIT] Virtual Memory Manager...\n");
    vmm_init();  /* identity-map 0-4MB, 启用分页 */

    printk("[INIT] Kernel Heap...\n");
    heap_init();

    /* ── PIT 时钟 (依赖 PMM 进行潜在的内核栈管理) ── */
    printk("[INIT] PIT... ");
    pit_init(100);
    printk("OK\n");

    /* ── 键盘 ── */
    printk("[INIT] Keyboard... ");
    keyboard_init();
    printk("OK\n");

    /* ── 测试内存分配 ── */
    printk("\n[M3] Testing memory allocation...\n");

    void *p1 = kmalloc(64);
    printk("[M3] kmalloc(64)  = 0x%x\n", (uint32_t)p1);

    void *p2 = kmalloc(4096);
    printk("[M3] kmalloc(4KB) = 0x%x\n", (uint32_t)p2);

    void *p3 = kmalloc(128);
    printk("[M3] kmalloc(128) = 0x%x\n", (uint32_t)p3);

    kfree(p1);
    printk("[M3] kfree(0x%x) OK\n", (uint32_t)p1);

    kfree(p3);
    kfree(p2);
    printk("[M3] All alloc/free tests passed.\n");

    /* ── 打印内存统计 ── */
    printk("\n[M3] Memory: %d / %d pages free (%d MB / %d MB)\n",
           pmm_free_pages(), pmm_total_pages(),
           (pmm_free_pages() * 4) / 1024,
           (pmm_total_pages() * 4) / 1024);

    /* ── 启动中断 + 进入空闲循环 ── */
    printk("\n[INIT] Enabling interrupts (STI)...\n");
    __asm__ volatile ("sti");

    printk("[M3] M3 initialized. System ready.\n\n");

    uint32_t last_tick = 0;
    while (1) {
        uint32_t cur = jiffies;
        if (cur != last_tick && (cur % 100) == 0) {
            last_tick = cur;
            printk("[TICK] jiffies=%d\n", cur);
        }

        /* 空闲: 等中断 */
        __asm__ volatile ("hlt");
    }
}
