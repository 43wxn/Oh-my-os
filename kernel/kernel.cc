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

    /* ── 逐步开启 M3, 定位异常源 ── */

    /* 测试 3a: 只读 E820 数据, 不初始化 PMM */
    uint32_t e820_count = *((uint32_t *)0x2000);
    printk("[3a] E820 raw count at 0x2000 = %d\n", e820_count);

    /* 测试 3b: 读取 E820 条目, 不分配任何东西 */
    if (e820_count > 0 && e820_count < 128) {
        for (uint32_t i = 0; i < e820_count && i < 8; i++) {
            uint32_t *entry = (uint32_t *)(0x2004 + i * 24);
            printk("[3b] Entry %d: base=0x%x%x len=0x%x%x type=%d\n",
                   i, entry[1], entry[0], entry[3], entry[2], entry[4]);
        }
    }

    /* 测试 3c: 初始化 PMM */
    printk("[3c] PMM init...\n");
    pmm_init();
    printk("[3c] PMM OK\n");

    /* 测试 3d: VMM */
    printk("[3d] VMM init...\n");
    vmm_init();
    printk("[3d] VMM OK, paging=%d\n", paging_is_enabled());

    /* 测试 3e: Heap */
    printk("[3e] Heap init...\n");
    heap_init();
    printk("[3e] Heap OK\n");

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
