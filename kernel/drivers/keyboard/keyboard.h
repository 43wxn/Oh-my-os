/* ============================================================================
 * kernel/drivers/keyboard/keyboard.h — PS/2 键盘驱动
 * ============================================================================ */

#ifndef _KERNEL_DRIVERS_KEYBOARD_KEYBOARD_H
#define _KERNEL_DRIVERS_KEYBOARD_KEYBOARD_H

#include <stdint.h>

/* 初始化键盘驱动 (注册 IRQ1 处理函数) */
void keyboard_init();

/* 从环形缓冲区读取一个字符 (阻塞) */
char kbd_getchar();

/* 是否有字符可读 (非阻塞) */
int kbd_haschar();

#endif /* _KERNEL_DRIVERS_KEYBOARD_KEYBOARD_H */
