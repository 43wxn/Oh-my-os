/* ============================================================================
 * kernel/mm/pmm.h — 物理内存管理器
 *
 * 基于位图的物理页框分配器。
 * 每 bit = 1 页 (4KB), 0=空闲, 1=已分配。
 * 位图位于内核 BSS 之后, 根据 E820 可用内存自动计算大小。
 * ============================================================================ */

#ifndef _KERNEL_MM_PMM_H
#define _KERNEL_MM_PMM_H

#include <stdint.h>

/* E820 内存区域类型 */
#define E820_USABLE      1
#define E820_RESERVED    2
#define E820_ACPI        3
#define E820_NVS         4
#define E820_UNUSABLE    5

/* E820 条目 (24 bytes, ACPI 3.0 格式) */
struct e820_entry_t {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attrs;
} __attribute__((packed));

/* API */
void     pmm_init();                     /* 初始化物理内存管理器        */
void*    pmm_alloc_page();               /* 分配一页 (4KB), 返回物理地址 */
void     pmm_free_page(void *phys_addr); /* 释放一页                     */
uint32_t pmm_total_pages();              /* 总可用页数                   */
uint32_t pmm_free_pages();               /* 剩余可用页数                 */

#endif /* _KERNEL_MM_PMM_H */
