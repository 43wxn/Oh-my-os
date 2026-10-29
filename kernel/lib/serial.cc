/* ============================================================================
 * kernel/lib/serial.cc — 串口调试输出 (COM1, 端口 0x3F8)
 *
 * 波特率 115200, 8N1, 无 FIFO。
 * 用于 QEMU -serial stdio 调试。
 * ============================================================================ */

#include "kernel/lib/serial.h"
#include "kernel/arch/x86/port.h"

#define COM1 0x3F8

void serial_init() {
    outb(COM1 + 1, 0x00);    /* 禁用中断                   */
    outb(COM1 + 3, 0x80);    /* DLAB = 1 (设置波特率)      */
    outb(COM1 + 0, 0x01);    /* Divisor LSB = 1 (115200)   */
    outb(COM1 + 1, 0x00);    /* Divisor MSB = 0            */
    outb(COM1 + 3, 0x03);    /* 8N1, DLAB = 0              */
    outb(COM1 + 2, 0xC7);    /* 启用 FIFO, 14-byte trigger */
    outb(COM1 + 4, 0x0B);    /* RTS/DTR/OUT2 启用          */
}

void serial_putchar(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {
        __asm__ volatile ("pause");
    }
    outb(COM1 + 0, (uint8_t)c);
}

void serial_print_hex(uint32_t val) {
    serial_print("0x");
    for (int i = 7; i >= 0; i--) {
        serial_putchar("0123456789abcdef"[(val >> (i * 4)) & 0xF]);
    }
}
