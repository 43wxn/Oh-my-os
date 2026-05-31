/* ============================================================================
 * kernel/kernel.cc — 内核主入口
 *
 * 这是系统从汇编跳转到 C++ 后执行的第一段代码。
 * M1 阶段只做一件事: 在 VGA 上打印信息, 证明内核启动成功。
 * ============================================================================ */

#include "kernel/printk.h"

/* 内核入口 — 由 boot.S 调用, 必须用 extern "C" 避免名字修饰 */
extern "C" void kernel_main() {
    /* 清屏 (VGA 文本模式, 80×25)
     * 0x0F20 = 空格(0x20) + 白字黑底(0x0F)
     * 注意: 不能用 0x0F00, VGA 上 NULL 字符可能显示为乱码 */
    uint16_t *vga = reinterpret_cast<uint16_t *>(0xB8000);
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }

    /* 用纯 ASCII 字符, 避免 VGA 字符集兼容问题 */
    printk("+------------------------------------------+\n");
    printk("|  Oh-my-os Kernel v0.0.1                  |\n");
    printk("+------------------------------------------+\n");
    printk("\n");
    printk("  [M1] Bootloader + Protected Mode  : OK\n");
    printk("\n");
    printk("  CPU   : Intel i5-8250U @ 1.60GHz\n");
    printk("  Arch  : x86 32-bit Protected Mode\n");
    printk("  Load  : 0x100000 (1MB)\n");
    printk("\n");
    printk("  [M2] Interrupt System (PIC+IDT+PIT)\n");
    printk("        ...coming next\n");
    printk("\n");
    printk("+------------------------------------------+\n");
    printk("  System halted.\n");
}
