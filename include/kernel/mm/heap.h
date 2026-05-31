/* ============================================================================
 * kernel/mm/heap.h — 内核堆分配器
 *
 * kmalloc/kfree: 简单链表 first-fit 分配器 + 页级 fallback。
 * ============================================================================ */

#ifndef _KERNEL_MM_HEAP_H
#define _KERNEL_MM_HEAP_H

#include <stdint.h>

void  heap_init();
void* kmalloc(uint32_t size);
void  kfree(void *ptr);
void* krealloc(void *ptr, uint32_t new_size);

#endif /* _KERNEL_MM_HEAP_H */
