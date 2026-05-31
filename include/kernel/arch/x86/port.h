/* ============================================================================
 * kernel/arch/x86/port.h — x86 I/O 端口操作
 *
 * 内联汇编封装 inb/outb/inw/outw/inl/outl。
 * 所有外设驱动 (PIC/PIT/键盘/硬盘) 都依赖这些函数。
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_PORT_H
#define _KERNEL_ARCH_X86_PORT_H

#include <stdint.h>

/* 读端口 (8-bit) */
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* 写端口 (8-bit) */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* 读端口 (16-bit) */
static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile ("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* 写端口 (16-bit) */
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* 读端口 (32-bit) */
static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* 写端口 (32-bit) */
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

/* I/O 延迟 (向未使用端口写, 等待总线周期完成)
 * 某些设备 (PIC, PIT) 在连续写入之间需要短暂延迟 */
static inline void io_wait() {
    outb(0x80, 0);
}

#endif /* _KERNEL_ARCH_X86_PORT_H */
