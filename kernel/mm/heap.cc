/* ============================================================================
 * kernel/mm/heap.cc — 内核堆分配器
 *
 * 当前: 整页分配器。kmalloc 取整到页大小, kfree 释放整页。
 * 优点: 无碎片, 极其简单。
 * 缺点: 小对象浪费空间 (~4KB 最小单位)。
 * 后续迭代: slab/slub 分配器或有头结点的 free-list。
 * ============================================================================ */

#include "kernel/mm/heap.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"

/* 堆虚拟地址范围 (3GB+, 内核空间高地址) */
#define HEAP_VBASE  0xD0000000
#define HEAP_MAX    0xE0000000

/* 块头 — 预留, 后续 free-list 实现用 */
struct block_hdr {
    uint32_t magic;           /* 魔数 (验证)         */
    uint32_t size;            /* 用户可用大小         */
};

#define BLOCK_MAGIC  0x4B4D414C  /* "KMAL" */

static uint32_t heap_top = HEAP_VBASE;  /* 堆顶 (下一可用地址) */

void heap_init() {
    /* 堆起始虚拟地址已由 vmm_alloc_vregion 预留。
     * 物理内存在首次 kmalloc 时按需分配 (Demand Paging 由 #PF 支持)。
     * 当前 #PF handler 尚未实现按需分页, 所以先预映射一部分 */
    printk("[HEAP] Heap range: 0x%x - 0x%x\n", HEAP_VBASE, HEAP_MAX);
}

void* kmalloc(uint32_t size) {
    if (size == 0) return nullptr;

    /* 对齐到页大小 (+ 块头) */
    uint32_t total = size + sizeof(block_hdr);
    uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    /* 检查堆空间上限 */
    if (heap_top + pages * PAGE_SIZE > HEAP_MAX) {
        printk("[HEAP] Out of heap space!\n");
        return nullptr;
    }

    /* 分配物理页并映射 */
    for (uint32_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            /* 回滚已分配的页 */
            for (uint32_t j = 0; j < i; j++) {
                uint32_t v = heap_top + j * PAGE_SIZE;
                uint32_t p = vmm_unmap_page(v);
                pmm_free_page((void *)p);
            }
            return nullptr;
        }
        uint32_t vaddr = heap_top + i * PAGE_SIZE;
        int rc = vmm_map_page(vaddr, (uint32_t)phys, PAGE_PRESENT | PAGE_RW);
        if (rc != 0) {
            pmm_free_page(phys);
            return nullptr;
        }
    }

    /* 写入块头 */
    block_hdr *hdr = (block_hdr *)heap_top;
    hdr->magic = BLOCK_MAGIC;
    hdr->size  = size;

    void *user_ptr = (void *)(heap_top + sizeof(block_hdr));
    heap_top += pages * PAGE_SIZE;

    return user_ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* 从用户指针回推块头 */
    block_hdr *hdr = (block_hdr *)((uint32_t)ptr - sizeof(block_hdr));
    if (hdr->magic != BLOCK_MAGIC) {
        printk("[HEAP] kfree: invalid block magic at 0x%x\n", (uint32_t)ptr);
        return;
    }

    uint32_t total = hdr->size + sizeof(block_hdr);
    uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t vaddr = (uint32_t)hdr;

    /* 解除映射 + 释放物理页 */
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t v = vaddr + i * PAGE_SIZE;
        uint32_t p = vmm_unmap_page(v);
        if (p) {
            pmm_free_page((void *)p);
        }
    }

    /* 注意: 不回收虚拟地址 (简化实现, 堆只增长不收缩) */
}

void* krealloc(void *ptr, uint32_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return nullptr; }

    block_hdr *hdr = (block_hdr *)((uint32_t)ptr - sizeof(block_hdr));
    if (hdr->magic != BLOCK_MAGIC) return nullptr;

    if (new_size <= hdr->size) {
        /* 缩小: 原地不动 */
        hdr->size = new_size;
        return ptr;
    }

    /* 扩大: 分配新块, 复制, 释放旧块 */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return nullptr;

    /* 手动复制 (不能用 memcpy, 因为我们还没实现) */
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (uint32_t i = 0; i < hdr->size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}
