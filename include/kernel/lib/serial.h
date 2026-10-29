/* ============================================================================
 * kernel/lib/serial.h — 串口调试输出 (COM1, 端口 0x3F8)
 * ============================================================================ */

#ifndef _KERNEL_LIB_SERIAL_H
#define _KERNEL_LIB_SERIAL_H

#include <stdint.h>

void serial_init();
void serial_putchar(char c);
void serial_print_hex(uint32_t val);

static inline void serial_print(const char *s) {
    if (!s) return;
    for (int i = 0; s[i] != '\0'; i++) {
        serial_putchar(s[i]);
    }
}

#endif /* _KERNEL_LIB_SERIAL_H */
