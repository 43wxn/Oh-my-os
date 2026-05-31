/* ============================================================================
 * kernel/mm/pmm.cc — 物理页框分配器 (位图法)
 *
 * 位图位于内核之后, 由链接脚本导出的 __kernel_end 确定起始位置。
 * 每 bit 代表一个 4KB 物理页。
 * ============================================================================ */

#include "kernel/mm/pmm.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"
#include <stdint.h>

/* ── 由链接脚本导出 ── */
extern uint8_t  __kernel_end;          /* 内核结束 (下一个可用字节)  */
extern uint8_t  __heap_start;          /* 堆起始                       */

/* ── 全局状态 ── */
static uint8_t  *bitmap = nullptr;     /* 位图指针                     */
static uint32_t  bitmap_pages = 0;     /* 位图占用的页数               */
static uint32_t  total_pages = 0;      /* 总物理页数                   */
static uint32_t  free_count = 0;       /* 空闲页数                     */
static uint32_t  mem_high = 0;         /* 最高可用物理地址             */

/* ── E820 数据位置 (stage2 写入) ── */
#define E820_BUF_ADDR   0x2000
#define E820_MAX_ENTRIES 128

/* ── 位图操作 ── */
static inline int   bitmap_test(uint32_t page);
static inline void  bitmap_set(uint32_t page);
static inline void  bitmap_clear(uint32_t page);

/* ── 初始化 ── */

void pmm_init() {
    uint32_t e820_count = *((uint32_t *)E820_BUF_ADDR);
    printk("[PMM] E820 count at 0x%x = %d\n", E820_BUF_ADDR, e820_count);

    /* ── 第一遍: 遍历 E820 找到最大物理地址 ── */
    uint64_t max_addr = 0;
    int e820_valid = 0;

    if (e820_count > 0 && e820_count < E820_MAX_ENTRIES) {
        e820_valid = 1;
        for (uint32_t i = 0; i < e820_count; i++) {
            e820_entry_t *entry =
                (e820_entry_t *)(E820_BUF_ADDR + 4 + i * sizeof(e820_entry_t));
            uint64_t end = entry->base + entry->length;
            if (end > max_addr) {
                max_addr = end;
            }
            printk("[PMM]   E820[%d]: base=0x%llx len=0x%llx type=%d\n",
                   i, entry->base, entry->length, entry->type);
        }
    }

    /* 32-bit 无 PAE, 物理地址上限 4GB */
    if (!e820_valid || max_addr == 0 || max_addr > 0x100000000ULL) {
        printk("[PMM] WARNING: E820 invalid or >4GB, capping at 4GB\n");
        max_addr = 0x100000000ULL;  /* 4GB */
    }

    total_pages = (uint32_t)(max_addr / PAGE_SIZE);
    mem_high    = total_pages * PAGE_SIZE;
    bitmap_pages = (total_pages / 8 + PAGE_SIZE - 1) / PAGE_SIZE;

    bitmap = (uint8_t *)&__kernel_end;
    uint32_t bitmap_bytes = bitmap_pages * PAGE_SIZE;

    printk("[PMM] Total pages: %d (%d MB)\n",
           total_pages, (uint32_t)(mem_high / (1024 * 1024)));
    printk("[PMM] Bitmap: %d pages (%d KB) at 0x%x\n",
           bitmap_pages, bitmap_bytes / 1024, (uint32_t)bitmap);

    /* ── 全部标记为已用 ── */
    for (uint32_t i = 0; i < bitmap_bytes; i++) {
        bitmap[i] = 0xFF;
    }
    free_count = 0;

    /* ── 第二遍: 标记 E820 可用区域为空闲 ── */
    if (e820_valid) {
        for (uint32_t i = 0; i < e820_count; i++) {
            e820_entry_t *entry =
                (e820_entry_t *)(E820_BUF_ADDR + 4 + i * sizeof(e820_entry_t));

            if (entry->type != E820_USABLE)
                continue;

            uint64_t base = entry->base;
            uint64_t end  = entry->base + entry->length;

            /* 限制在 4GB 以内 */
            if (base >= 0x100000000ULL)
                continue;
            if (end > 0x100000000ULL)
                end = 0x100000000ULL;

            uint32_t start_page = (uint32_t)(base / PAGE_SIZE);
            uint32_t end_page   = (uint32_t)((end + PAGE_SIZE - 1) / PAGE_SIZE);

            if (end_page > total_pages)
                end_page = total_pages;

            for (uint32_t p = start_page; p < end_page; p++) {
                if (bitmap_test(p)) {
                    bitmap_clear(p);
                    free_count++;
                }
            }

            printk("[PMM]   Free: 0x%llx - 0x%llx (%d MB) type=%d\n",
                   base, end, (uint32_t)((end - base) / (1024 * 1024)),
                   entry->type);
        }
    } else {
        /* E820 数据无效, 回退到硬编码 (ASUS X542UF 安全布局) */
        printk("[PMM] E820 invalid, using fallback map\n");

        /* 0-640KB */
        for (uint32_t p = 0; p < 0xA0000 / PAGE_SIZE; p++) {
            bitmap_clear(p); free_count++;
        }
        /* 1MB-4GB */
        for (uint32_t p = 0x100000 / PAGE_SIZE; p < total_pages; p++) {
            bitmap_clear(p); free_count++;
        }

        printk("[PMM]   Fallback: 0x0-0xA0000 + 0x100000-0x%x\n", mem_high);
    }

    /* ── 标记内核占用的页为已用 ── */
    uint32_t kernel_end_page =
        ((uint32_t)(&__kernel_end) + bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 0; p < kernel_end_page && p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    /* ── 标记 VGA / BIOS 区域为已用 (二次确认) ── */
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
