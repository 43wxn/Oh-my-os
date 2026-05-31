/* ============================================================================
 * kernel/mm/vmm.cc — 虚拟内存管理器
 *
 * 当前策略: 内核空间 identity mapping (vaddr = paddr)
 * 页目录物理地址保存在全局变量中。
 * ============================================================================ */

#include "kernel/mm/vmm.h"
#include "kernel/mm/pmm.h"
#include "kernel/printk.h"
#include <stdint.h>

/* ── 当前地址空间的页目录物理地址 ── */
static uint32_t current_pd_phys = 0;

/* ── PDE/PTE 访问 ──
 * 使用递归页表技巧: PDE 最后一项指向 PD 自身,
 * 则 PD 可被访问为虚拟地址 0xFFFFF000,
 * PT 可被访问为 0xFFC00000 + (pde_idx << 12)
 *
 * 简化: 直接用 identity mapping 下的物理地址访问页表
 * (因为内核段是 identity-mapped 的) */

static inline uint32_t* get_pd() {
    return (uint32_t*)current_pd_phys;
}

static inline uint32_t* get_pt(uint32_t pde_idx) {
    uint32_t *pd = get_pd();
    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        return nullptr;
    }
    return (uint32_t*)(pd[pde_idx] & PAGE_MASK);
}

/* ── 切换页目录 ── */
void vmm_switch_pd(uint32_t pd_phys) {
    current_pd_phys = pd_phys;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(pd_phys) : "memory");
}

/* ── 初始化 ── */

void vmm_init() {
    /* 分配页目录 (4KB 对齐, pmm_alloc_page 返回 4KB 对齐的物理地址) */
    uint32_t *pd = (uint32_t *)pmm_alloc_page();
    if (!pd) {
        printk("[VMM] FATAL: Cannot allocate page directory!\n");
        return;
    }
    current_pd_phys = (uint32_t)pd;

    /* 清零页目录 */
    for (int i = 0; i < 1024; i++) {
        pd[i] = 0;
    }

    /* 分配第一个页表 (覆盖 0-4MB, identity map) */
    uint32_t *pt0 = (uint32_t *)pmm_alloc_page();
    if (!pt0) {
        printk("[VMM] FATAL: Cannot allocate page table 0!\n");
        return;
    }

    /* 填充页表 0: identity map 0-4MB
     * PTE[i] = (i * 4096) | flags
     * flags: Present, Read/Write, 内核 */
    for (int i = 0; i < 1024; i++) {
        pt0[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }

    /* PDE[0] → PT0 */
    pd[0] = ((uint32_t)pt0) | PAGE_PRESENT | PAGE_RW;

    /* 加载 CR3, 启用分页 */
    paging_load_cr3(current_pd_phys);
    paging_enable();

    printk("[VMM] Page directory at phys 0x%x\n", current_pd_phys);
    printk("[VMM] Page table 0 (0-4MB) identity-mapped\n");
    printk("[VMM] Paging enabled (CR0.PG=1)\n");
}

/* ── 映射页 ── */

int vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    uint32_t *pd = get_pd();
    uint32_t pde_idx = PD_INDEX(vaddr);
    uint32_t pte_idx = PT_INDEX(vaddr);

    /* 如果 PDE 不存在, 分配新的页表 */
    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        uint32_t *new_pt = (uint32_t *)pmm_alloc_page();
        if (!new_pt) {
            return -1;            /* OOM */
        }
        /* 清零新页表 */
        for (int i = 0; i < 1024; i++) {
            new_pt[i] = 0;
        }
        pd[pde_idx] = ((uint32_t)new_pt) | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    }

    uint32_t *pt = get_pt(pde_idx);
    if (!pt) {
        return -1;
    }

    pt[pte_idx] = (paddr & PAGE_MASK) | (flags & 0xFFF) | PAGE_PRESENT;

    /* 刷新 TLB */
    tlb_flush_page(vaddr);

    return 0;
}

/* ── 解除映射 ── */

uint32_t vmm_unmap_page(uint32_t vaddr) {
    uint32_t *pd = get_pd();
    uint32_t pde_idx = PD_INDEX(vaddr);
    uint32_t pte_idx = PT_INDEX(vaddr);

    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        return 0;                  /* PDE 不存在 */
    }

    uint32_t *pt = get_pt(pde_idx);
    if (!pt || !(pt[pte_idx] & PAGE_PRESENT)) {
        return 0;                  /* PTE 不存在 */
    }

    uint32_t old_paddr = pt[pte_idx] & PAGE_MASK;
    pt[pte_idx] = 0;

    tlb_flush_page(vaddr);
    return old_paddr;
}

/* ── 虚拟→物理 ── */

uint32_t vmm_virt_to_phys(uint32_t vaddr) {
    uint32_t *pd = get_pd();
    uint32_t pde_idx = PD_INDEX(vaddr);
    uint32_t pte_idx = PT_INDEX(vaddr);

    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t *pt = get_pt(pde_idx);
    if (!pt || !(pt[pte_idx] & PAGE_PRESENT)) {
        return 0;
    }

    return (pt[pte_idx] & PAGE_MASK) | PAGE_OFFSET(vaddr);
}

/* ── 分配连续虚拟地址区域 ── */

uint32_t vmm_alloc_vregion(uint32_t size_pages) {
    /* 简单实现: 从内核堆区域 (3GB+) 线性分配
     * 当前返回固定地址, 后续用更完善的 VAD tree */
    static uint32_t next_vaddr = 0xC0000000;  /* 3GB 以上 = 内核空间 */
    uint32_t start = next_vaddr;
    next_vaddr += size_pages * PAGE_SIZE;
    return start;
}
