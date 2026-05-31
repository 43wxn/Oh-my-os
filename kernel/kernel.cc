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
    printk("[1] IDT...\n");
    idt_init();
    printk("[1] IDT OK\n");

    printk("[2] PIC...\n");
    pic_init();
    printk("[2] PIC OK\n");

    /* ── M3: 内存管理 ──
     * PMM 从 stage2 写入的 E820 数据 (0x2000) 获取物理内存布局,
     * 通过两次遍历建立位图; E820 无效时自动回退 Safe-Zone. */

    /* E820 summary (PMM 初始化前先看一眼数据) */
    uint32_t e820_count = *((uint32_t *)0x2000);
    printk("[3a] E820 entries at 0x2000: %d\n", e820_count);
    if (e820_count > 0 && e820_count < 128) {
        for (uint32_t i = 0; i < e820_count && i < 8; i++) {
            e820_entry_t *e = (e820_entry_t *)(0x2004 + i * sizeof(e820_entry_t));
            printk("     [%d] base=%x%x len=%x%x type=%d\n",
                   i,
                   (uint32_t)(e->base >> 32), (uint32_t)e->base,
                   (uint32_t)(e->length >> 32), (uint32_t)e->length,
                   e->type);
        }
    }

    printk("[3b] PMM init...\n");
    pmm_init();
    printk("[3b] PMM OK\n");

    printk("[3c] VMM init...\n");
    vmm_init();
    printk("[3c] VMM OK, paging=%d\n", paging_is_enabled());

    printk("[3d] Heap init...\n");
    heap_init();
    printk("[3d] Heap OK\n");

    /* ── 暂停, 等用户看完 M3 初始化信息 ── */
    printk("\n----------------------------------------\n");
    printk("Press any key to start the kernel...\n");
    keyboard_init();
    __asm__ volatile ("sti");
    kbd_getchar();  /* 阻塞, 等待按键 */
    __asm__ volatile ("cli");

    /* ── 进入运行循环 ── */
    printk("\n[3] PIT...\n");
    pit_init(100);
    printk("[3] Running. jiffies=%d\n\n", jiffies);
    __asm__ volatile ("sti");

    uint32_t last_tick = 0;
    while (1) {
        uint32_t cur = jiffies;
        if (cur != last_tick && (cur % 100) == 0) {
            last_tick = cur;
            printk("[T] %d\n", cur);
        }
        __asm__ volatile ("hlt");
    }
}
