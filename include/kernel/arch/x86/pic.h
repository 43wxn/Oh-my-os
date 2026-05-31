/* ============================================================================
 * kernel/arch/x86/pic.h — 8259A 可编程中断控制器
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_PIC_H
#define _KERNEL_ARCH_X86_PIC_H

#include <stdint.h>

/* I/O 端口 */
#define PIC1_CMD    0x20        /* 主片 命令端口 */
#define PIC1_DATA   0x21        /* 主片 数据端口 */
#define PIC2_CMD    0xA0        /* 从片 命令端口 */
#define PIC2_DATA   0xA1        /* 从片 数据端口 */

/* 初始化控制字 */
#define ICW1_ICW4   0x01        /* 需要 ICW4 */
#define ICW1_INIT   0x10        /* 初始化命令 */

/* 重映射向量号 */
#define PIC1_OFFSET 0x20        /* IRQ0-7 → 0x20-0x27 */
#define PIC2_OFFSET 0x28        /* IRQ8-15 → 0x28-0x2F */

/* 命令 */
#define PIC_EOI     0x20        /* End Of Interrupt */

void pic_init();                /* 初始化 + 重映射 */
void pic_send_eoi(uint8_t irq); /* 发送 EOI */
void pic_set_mask(uint8_t irq); /* 屏蔽指定 IRQ */
void pic_clear_mask(uint8_t irq); /* 取消屏蔽 */

#endif /* _KERNEL_ARCH_X86_PIC_H */
