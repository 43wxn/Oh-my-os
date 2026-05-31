/* ============================================================================
 * kernel/arch/x86/isr.h — 中断服务例程
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_ISR_H
#define _KERNEL_ARCH_X86_ISR_H

#include "kernel/arch/x86/idt.h"

/* 中断处理函数指针类型 */
typedef void (*irq_handler_t)(int_frame_t *frame);

/* 注册 IRQ 处理函数 */
void irq_register(uint8_t irq, irq_handler_t handler);

/* 由汇编调用的 C++ 分发器 */
extern "C" {
    void isr_handler(int_frame_t *frame);
    void irq_handler(int_frame_t *frame);
}

#endif /* _KERNEL_ARCH_X86_ISR_H */
