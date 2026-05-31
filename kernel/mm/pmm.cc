/* ============================================================================
 * kernel/mm/pmm.cc — 物理页框分配器 (位图法)
 *
 * 位图位于内核之后, 由链接脚本导出的 __kernel_end 确定起始位置。
 * 每 bit 代表一个 4KB 物理页。
 *
 * 内存发现策略 (商业 OS 标准做法):
 *   1. stage2 在实模式下通过 BIOS INT 0x15 E820 获取物理内存布局,
 *      写入 0x2000: [count:dword][entries: e820_entry_t ...]
 *   2. pmm_init() 两次遍历 E820:
 *      Pass 1 — 找到最大物理地址, 确定位图覆盖范围
 *      Pass 2 — 标记 type=1 (Usable) 的区域为空闲页
 *   3. 最后标记内核/VGA/BIOS 区域为已占用
 *   4. E820 数据无效时回退 Safe-Zone (1MB-128MB)
 * ============================================================================ */

#include "kernel/mm/pmm.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"
#include <stdint.h>

/* ── 由链接脚本导出 ── */
extern uint8_t  __kernel_end;          /* 内核结束 (下一个可用字节)  */
extern uint8_t  __heap_start;          /* 堆起始                       */

/* ── E820 缓冲区 (stage2 写入) ── */
#define E820_BUF       0x2000
#define E820_MAX       128

/* ── Fallback: Safe-Zone ── */
#define FALLBACK_BASE  0x100000        /* 1MB                          */
#define FALLBACK_PAGES ((128 * 1024 * 1024) / PAGE_SIZE)  /* 128MB    */

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
    uint32_t e820_count = *((uint32_t *)E820_BUF);
    int      e820_ok = 0;

    printk("[PMM] E820 buffer at 0x%x, count=%d\n", E820_BUF, e820_count);

    /* ── 验证 E820 数据有效性 ── */
    if (e820_count > 0 && e820_count < E820_MAX) {
        e820_ok = 1;
    } else {
        printk("[PMM] E820 data invalid (count=%d), fallback to Safe-Zone\n",
               e820_count);
    }

    /* ════════════════════════════════════════════════════════════════
     * Pass 1: 遍历 E820, 找最大物理地址
     * ════════════════════════════════════════════════════════════════ */
    uint64_t max_addr = 0;

    if (e820_ok) {
        for (uint32_t i = 0; i < e820_count; i++) {
            e820_entry_t *e = (e820_entry_t *)(E820_BUF + 4
                                               + i * sizeof(e820_entry_t));
            uint64_t end = e->base + e->length;
            if (end > max_addr) {
                max_addr = end;
            }
        }
    }

    /* 32 位无 PAE: 物理地址上限 4GB */
    if (max_addr == 0 || max_addr > 0x100000000ULL) {
        max_addr = 0x100000000ULL;  /* 4GB */
    }

    total_pages  = (uint32_t)(max_addr / PAGE_SIZE);
    bitmap_pages = (total_pages / 8 + PAGE_SIZE - 1) / PAGE_SIZE;
    bitmap       = (uint8_t *)&__kernel_end;
    uint32_t bitmap_bytes = bitmap_pages * PAGE_SIZE;

    printk("[PMM] Max phys addr: %x%x, total_pages=%d (%d MB)\n",
           (uint32_t)(max_addr >> 32), (uint32_t)max_addr,
           total_pages, (uint32_t)(max_addr / (1024 * 1024)));
    printk("[PMM] Bitmap: %d pages (%d KB) at 0x%x\n",
           bitmap_pages, bitmap_bytes / 1024, (uint32_t)bitmap);

    /* ── 位图全部初始化为 "已占用" ── */
    for (uint32_t i = 0; i < bitmap_bytes; i++) {
        bitmap[i] = 0xFF;
    }
    free_count = 0;

    /* ════════════════════════════════════════════════════════════════
     * Pass 2: 遍历 E820, 标记可用区域为空闲
     * ════════════════════════════════════════════════════════════════ */
    if (e820_ok) {
        for (uint32_t i = 0; i < e820_count; i++) {
            e820_entry_t *e = (e820_entry_t *)(E820_BUF + 4
                                               + i * sizeof(e820_entry_t));

            if (e->type != E820_USABLE)
                continue;

            uint64_t base = e->base;
            uint64_t end  = e->base + e->length;

            /* 限制在 32 位地址空间内 */
            if (base >= 0x100000000ULL) continue;
            if (end  >  0x100000000ULL) end = 0x100000000ULL;

            uint32_t pg_start = (uint32_t)(base / PAGE_SIZE);
            uint32_t pg_end   = (uint32_t)((end + PAGE_SIZE - 1) / PAGE_SIZE);
            if (pg_end > total_pages) pg_end = total_pages;

            for (uint32_t p = pg_start; p < pg_end; p++) {
                if (bitmap_test(p)) {
                    bitmap_clear(p);
                    free_count++;
                }
            }

            printk("[PMM]   [%d] %x%x - %x%x  type=%d  (%d MB)\n",
                   i,
                   (uint32_t)(base >> 32), (uint32_t)base,
                   (uint32_t)(end >> 32), (uint32_t)end,
                   e->type, (uint32_t)((end - base) / (1024 * 1024)));
        }
    } else {
        /* ── Fallback: Safe-Zone 1MB-128MB ── */
        printk("[PMM] Using Safe-Zone fallback: 0x%x - 0x%x\n",
               FALLBACK_BASE, FALLBACK_BASE + FALLBACK_PAGES * PAGE_SIZE);

        for (uint32_t p = FALLBACK_BASE / PAGE_SIZE;
             p < FALLBACK_BASE / PAGE_SIZE + FALLBACK_PAGES;
             p++) {
            if (bitmap_test(p)) {
                bitmap_clear(p);
                free_count++;
            }
        }
    }

    /* ════════════════════════════════════════════════════════════════
     * 标记保留区域为已占用
     * ════════════════════════════════════════════════════════════════ */

    /* 内核 + 位图 (0x100000 → __kernel_end + bitmap) */
    uint32_t reserved_end =
        ((uint32_t)(&__kernel_end) + bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 0; p < reserved_end && p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    /* VGA 显存 + BIOS (0xA0000 - 0xFFFFF) */
    for (uint32_t p = 0xA0000 / PAGE_SIZE; p < 0x100000 / PAGE_SIZE; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_count--;
        }
    }

    printk("[PMM] Reserved: kernel+bitmap %d pages, VGA/BIOS 0xA0-0xFF\n",
           reserved_end);
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
