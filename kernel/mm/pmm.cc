/* ============================================================================
 * kernel/mm/pmm.cc — 物理页框分配器 (位图法, Safe-Zone 策略)
 *
 * 位图位于内核之后, 由链接脚本导出的 __kernel_end 确定起始位置。
 * 每 bit 代表一个 4KB 物理页。
 *
 * Safe-Zone 策略:
 *   在所有 x86 机器上, 1MB (0x100000) 到 128MB (0x8000000) 这个区间
 *   几乎 100% 是连续的可用 RAM, 没有 MMIO 也没有 ACPI 表。
 *   我们不依赖 E820 (BIOS 实机兼容性差), 直接用这个安全区。
 *   等后续 M8+ (ACPI) 阶段再正规化物理内存管理。
 * ============================================================================ */

#include "kernel/mm/pmm.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"
#include <stdint.h>

/* ── 由链接脚本导出 ── */
extern uint8_t  __kernel_end;          /* 内核结束 (下一个可用字节)  */
extern uint8_t  __heap_start;          /* 堆起始                       */

/* ── Safe-Zone 常量 ── */
#define SAFE_ZONE_BASE  0x100000       /* 1MB                          */
#define SAFE_ZONE_SIZE  (127 * 1024 * 1024)  /* 127MB (1MB-128MB)      */
#define SAFE_ZONE_PAGES (SAFE_ZONE_SIZE / PAGE_SIZE)

/* ── 全局状态 ── */
static uint8_t  *bitmap = nullptr;     /* 位图指针                     */
static uint32_t  bitmap_pages = 0;     /* 位图占用的页数               */
static uint32_t  total_pages = 0;      /* 总物理页数                   */
static uint32_t  free_count = 0;       /* 空闲页数                     */

/* ── 位图操作 ── */
static inline int   bitmap_test(uint32_t page);
static inline void  bitmap_set(uint32_t page);
static inline void  bitmap_clear(uint32_t page);

/* ── 初始化 ── */

void pmm_init() {
    printk("[PMM] Safe-Zone: 1MB-128MB hardcoded (no E820 dependency)\n");

    /* 只管理 0-128MB 区间 */
    total_pages   = (SAFE_ZONE_BASE + SAFE_ZONE_SIZE) / PAGE_SIZE;
    bitmap_pages  = (total_pages / 8 + PAGE_SIZE - 1) / PAGE_SIZE;
    bitmap        = (uint8_t *)&__kernel_end;
    uint32_t bitmap_bytes = bitmap_pages * PAGE_SIZE;

    printk("[PMM] Managing %d pages (%d MB), bitmap %d KB at 0x%x\n",
           total_pages, (total_pages * 4096) / (1024 * 1024),
           bitmap_bytes / 1024, (uint32_t)bitmap);

    /* 全部标记为已用 */
    for (uint32_t i = 0; i < bitmap_bytes; i++) {
        bitmap[i] = 0xFF;
    }
    free_count = 0;

    /* 只标记 Safe-Zone (1MB-128MB) 为空闲 */
    uint32_t safe_start = SAFE_ZONE_BASE / PAGE_SIZE;
    uint32_t safe_end   = safe_start + SAFE_ZONE_PAGES;

    for (uint32_t p = safe_start; p < safe_end && p < total_pages; p++) {
        bitmap_clear(p);
        free_count++;
    }

    printk("[PMM]   Safe zone: 0x%x - 0x%x (%d MB)\n",
           SAFE_ZONE_BASE, SAFE_ZONE_BASE + SAFE_ZONE_SIZE,
           SAFE_ZONE_SIZE / (1024 * 1024));

    /* 标记内核占用的页为已用 (内核在 0x100000+, 位于 safe zone 内) */
    uint32_t kernel_end_page =
        ((uint32_t)(&__kernel_end) + bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 0; p < kernel_end_page && p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    printk("[PMM] Kernel reserved: %d pages (up to 0x%x)\n",
           kernel_end_page, kernel_end_page * PAGE_SIZE);

    /* 标记 VGA/BIOS 区域 (0xA0000-0xFFFFF) 为已用 */
    for (uint32_t p = 0xA0000 / PAGE_SIZE; p < 0x100000 / PAGE_SIZE; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    printk("[PMM] Free pages: %d (%d MB)\n",
           free_count, (free_count * 4096) / (1024 * 1024));
}

/* ── 分配 / 释放 ── */

void* pmm_alloc_page() {
    /* 线性扫描找空闲页 */
    for (uint32_t p = 1; p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
            return (void *)(p * PAGE_SIZE);
        }
    }
    printk("[PMM] ERROR: Out of memory!\n");
    return nullptr;
}

void pmm_free_page(void *phys_addr) {
    uint32_t page = (uint32_t)phys_addr / PAGE_SIZE;
    if (page >= total_pages) {
        return;
    }
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_count++;
    }
}

uint32_t pmm_total_pages() { return total_pages; }
uint32_t pmm_free_pages()  { return free_count; }

/* ── 位图原子操作 ── */

static inline int bitmap_test(uint32_t page) {
    return (bitmap[page / 8] >> (page % 8)) & 1;
}

static inline void bitmap_set(uint32_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static inline void bitmap_clear(uint32_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}
