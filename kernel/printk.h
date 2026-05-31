/* ============================================================================
 * include/kernel/printk.h — 内核格式化输出 + 滚动回溯
 * ============================================================================ */

#ifndef _KERNEL_PRINTK_H
#define _KERNEL_PRINTK_H

#include <stdint.h>

/* VGA 文本模式常量 */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  reinterpret_cast<uint16_t *>(0xB8000)

/* 输出格式化字符串到 VGA */
void printk(const char *fmt, ...);

/* 输出单个字符 */
void putchar(char c);

/* 清屏 */
void clear_screen();

/* ── 滚动回溯 API ── */
void console_scroll_up(int lines);       /* 向上滚动 n 行       */
void console_scroll_down(int lines);     /* 向下滚动 n 行       */
void console_scroll_reset();             /* 退出滚动, 回到实时   */
int  console_is_scrolling();             /* 是否处于滚动模式     */

#endif /* _KERNEL_PRINTK_H */
