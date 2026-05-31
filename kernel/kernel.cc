/* ============================================================================
 * kernel/kernel.cc — 内核主函数 (M4: 进程管理 + 控制台滚动)
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

extern volatile uint32_t jiffies;

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

    /* ── M4: 进程管理 ── */
    printk("[4a] Proc init...\n");
    proc_init();

    static pcb boot_pcb;
    proc_set_current(&boot_pcb);

    /* ── 生成大量测试输出以验证滚动回溯 ── */
    printk("\n--- Scrollback test: 50 lines ---\n");
    for (int i = 0; i < 50; i++) {
        printk("  Line %d: The quick brown fox jumps over the lazy dog.\n", i);
    }
    printk("--- End of scrollback test ---\n\n");

    /* ── 启动键盘 + PIT ── */
    printk("[5] Keyboard...\n");
    keyboard_init();
    printk("[5] Keyboard OK\n");

    printk("[6] PIT...\n");
    pit_init(100);
    printk("[6] PIT OK\n");

    printk("\n==========================================\n");
    printk("  Use UP/DOWN/PgUp/PgDn to scroll output\n");
    printk("  Press any key to exit scroll mode\n");
    printk("==========================================\n\n");
    __asm__ volatile ("sti");

    /* ── 运行循环: 显示 tick 计数 ── */
    uint32_t last = 0;
    while (1) {
        uint32_t cur = jiffies;
        if (cur != last && (cur % 100) == 0) {
            last = cur;
            printk("[T] %d  ", cur);
        }
        __asm__ volatile ("hlt");
    }
}
