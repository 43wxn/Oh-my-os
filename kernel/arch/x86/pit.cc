/* ============================================================================
 * kernel/arch/x86/pit.cc — PIT 8253 时钟中断
 *
 * 通道 0 → IRQ0 → 每 10ms 触发一次中断处理
 * ============================================================================ */

#include "kernel/arch/x86/pit.h"
#include "kernel/arch/x86/port.h"
#include "kernel/arch/x86/isr.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/printk.h"

/* 启动以来的时钟滴答数 */
volatile uint32_t jiffies = 0;

/* 时钟中断处理函数 */
static void pit_handler(int_frame_t *) {
    jiffies++;
}

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;

    if (divisor == 0 || divisor > 65535) {
        divisor = PIT_BASE_FREQ / PIT_DEFAULT_HZ;
    }

    /* 模式 3 (方波发生器), 通道 0, 读写两字节, 二进制 */
    outb(PIT_CMD, 0x36);

    /* 实机需要充分的 I/O 延迟: PIT 内部时钟远慢于 CPU,
     * outb(0x80) 只有 1 个总线周期, 不够 PIT 锁存数据.
     * 用多次 I/O + 忙等组合确保 PIT 收到所有字节 */
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile ("pause");
    }

    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));

    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile ("pause");
    }

    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile ("pause");
    }

    /* 验证: 读回 PIT 当前计数值 (通过锁存命令) */
    outb(PIT_CMD, 0x00);        /* 锁存通道 0 计数器 */
    for (volatile int i = 0; i < 100; i++) {}
    uint8_t lo = inb(PIT_CH0_DATA);
    uint8_t hi = inb(PIT_CH0_DATA);

    /* 注册 IRQ0 处理函数 */
    irq_register(0, pit_handler);

    /* 取消屏蔽 IRQ0 */
    pic_clear_mask(0);

    printk("[PIT] Initialized: %d Hz (divisor=%d, counter=%d)\n",
           frequency_hz, divisor, (hi << 8) | lo);
}
