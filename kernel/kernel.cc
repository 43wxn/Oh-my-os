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

    /* ── M3: Safe-Zone 内存管理 (跳过 E820, 直接硬编码) ── */

    /* VGA 直接写入: 红色标记确认 pic_init 即将执行 */
    uint16_t *vga_mark = (uint16_t *)0xB8000 + 80 * 3 + 0;
    vga_mark[0] = (0x4F << 8) | 'A';  /* 'A' 红底白字 */

    pic_init();
    vga_mark[1] = (0x2F << 8) | 'B';  /* 'B' 绿底白字 — 如果看到, pic_init 成功 */

    printk("[2] PIC OK\n");

    /* PMM: Safe-Zone (1MB-128MB) */
    printk("[3a] PMM init...\n");
    pmm_init();
    printk("[3a] PMM OK\n");

    /* VMM */
    printk("[3b] VMM init...\n");
    vmm_init();
    printk("[3b] VMM OK, paging=%d\n", paging_is_enabled());

    /* Heap */
    printk("[3c] Heap init...\n");
    heap_init();
    printk("[3c] Heap OK\n");

    /* 返回 M2 风格的运行模式 */
    printk("[3] PIT...\n");
    pit_init(100);
    printk("[3] Keyboard...\n");
    keyboard_init();
    printk("[3] STI...\n");
    __asm__ volatile ("sti");

    printk("[3] Running. jiffies=%d\n\n", jiffies);

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
