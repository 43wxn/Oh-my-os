/* ============================================================================
 * kernel/mm/pmm.cc — 物理页框分配器 (位图法)
 *
 * 位图位于内核之后, 由链接脚本导出的 __kernel_end 确定起始位置。
 * 每 bit 代表一个 4KB 物理页。支持 8GB 物理内存只需 256KB 位图。
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
#define E820_BUF    ((uint32_t *)0x9000)

/* ── 位图操作 ── */
static inline int   bitmap_test(uint32_t page);
static inline void  bitmap_set(uint32_t page);
static inline void  bitmap_clear(uint32_t page);

/* ── 初始化 ── */

void pmm_init() {
    uint32_t count = *E820_BUF;
    e820_entry_t *entries = (e820_entry_t *)(E820_BUF + 1);

    printk("[PMM] E820 count raw: %d (0x%x)\n", count, count);

    /* 安全阀: 如果 E820 数据明显损坏, 用硬编码 fallback */
    if (count == 0 || count > 128) {
        printk("[PMM] E820 data invalid, using fallback map\n");
        count = 0;  /* 跳过 E820 解析, 用硬编码 */
    }

    /* 第一遍: 找到最高可用物理地址, 计算总页数 */
    uint64_t highest = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t end = entries[i].base + entries[i].length;
        if (entries[i].type == E820_USABLE && end > highest) {
            highest = end;
        }
    }

    /* 硬编码 fallback: 8GB (0x200000000) */
    if (highest < 0x100000) {
        highest = 0x200000000ULL;  /* 8GB */
    }

    mem_high = (uint32_t)highest;
    total_pages = mem_high / PAGE_SIZE;
    bitmap_pages = (total_pages / 8 + PAGE_SIZE - 1) / PAGE_SIZE;

    /* 位图紧接内核 */
    bitmap = (uint8_t *)&__kernel_end;
    uint32_t bitmap_bytes = bitmap_pages * PAGE_SIZE;

    printk("[PMM] Total pages: %d (%d MB)\n",
           total_pages, (mem_high / (1024 * 1024)));
    printk("[PMM] Bitmap: %d pages (%d KB) at 0x%x\n",
           bitmap_pages, bitmap_bytes / 1024, (uint32_t)bitmap);

    /* 默认: 所有页标记为已用 */
    for (uint32_t i = 0; i < total_pages / 8; i++) {
        bitmap[i] = 0xFF;
    }
    free_count = 0;

    /* 第二遍: 标记可用区域为 0 (空闲) */
    if (count > 0) {
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].type == E820_USABLE) {
                uint64_t base = entries[i].base;
                uint64_t len  = entries[i].length;
                uint32_t start_page = (uint32_t)(base / PAGE_SIZE);
                uint32_t end_page   = (uint32_t)((base + len) / PAGE_SIZE);

                for (uint32_t p = start_page; p < end_page; p++) {
                    bitmap_clear(p);
                    free_count++;
                }
                printk("[PMM]   Usable: 0x%x - 0x%x (%d MB)\n",
                       (uint32_t)base, (uint32_t)(base + len),
                       (uint32_t)(len / (1024 * 1024)));
            }
        }
    } else {
        /* 硬编码 fallback: 0-640KB + 1MB-8GB 为可用 */
        printk("[PMM]   Using hardcoded map for 8GB\n");
        uint32_t sp, ep;

        /* 0-640KB */
        sp = 0; ep = 0xA0000 / PAGE_SIZE;
        for (uint32_t p = sp; p < ep; p++) { bitmap_clear(p); free_count++; }
        printk("[PMM]   Usable: 0x0 - 0xA0000 (640 KB)\n");

        /* 1MB - 8GB */
        sp = 0x100000 / PAGE_SIZE; ep = mem_high / PAGE_SIZE;
        for (uint32_t p = sp; p < ep; p++) { bitmap_clear(p); free_count++; }
        printk("[PMM]   Usable: 0x100000 - 0x%x (%d MB)\n",
               mem_high, (mem_high - 0x100000) / (1024*1024));
    }

    /* 标记内核占用的页为已用 (0 → __kernel_end + bitmap) */
    uint32_t kernel_end_page = ((uint32_t)(&__kernel_end) + bitmap_bytes) / PAGE_SIZE + 1;
    for (uint32_t p = 0; p < kernel_end_page; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    /* 标记 VGA 显存和 BIOS 区域 (<1MB 的保留区) 为已用 */
    /* (E820 已经标记为 reserved, 但再确认一下) */
    for (uint32_t p = 0x000A0; p < 0x00100; p++) {  /* 0xA0000-0xFFFFF */
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
