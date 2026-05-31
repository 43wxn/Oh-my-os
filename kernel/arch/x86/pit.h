/* ============================================================================
 * kernel/arch/x86/pit.h — 8253/8254 可编程间隔定时器
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_PIT_H
#define _KERNEL_ARCH_X86_PIT_H

#include <stdint.h>

#define PIT_CH0_DATA  0x40
#define PIT_CH1_DATA  0x41
#define PIT_CH2_DATA  0x42
#define PIT_CMD       0x43

/* 默认频率 100Hz (每 10ms 一个 tick) */
#define PIT_BASE_FREQ  1193180
#define PIT_DEFAULT_HZ 100

/* 全局 tick 计数 */
extern volatile uint32_t jiffies;

void pit_init(uint32_t frequency_hz);

#endif /* _KERNEL_ARCH_X86_PIT_H */
