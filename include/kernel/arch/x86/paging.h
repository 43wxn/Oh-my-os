/* ============================================================================
 * kernel/arch/x86/paging.h — x86 分页硬件操作
 *
 * 二级页表 (10-10-12):
 *   CR3 → Page Directory (1024 PDEs) → Page Table (1024 PTEs) → 物理页
 *   PDE: bits 31-12 = PT base, bits 11-0 = flags
 *   PTE: bits 31-12 = Page base, bits 11-0 = flags
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_PAGING_H
#define _KERNEL_ARCH_X86_PAGING_H

#include <stdint.h>

/* ── 页表常量 ── */
#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define PAGE_MASK        0xFFFFF000

#define PD_INDEX(vaddr)  (((uint32_t)(vaddr) >> 22) & 0x3FF)
#define PT_INDEX(vaddr)  (((uint32_t)(vaddr) >> 12) & 0x3FF)
#define PAGE_OFFSET(v)   ((uint32_t)(v) & 0xFFF)

/* ── PDE/PTE 标志 ── */
#define PAGE_PRESENT   0x01   /* 页存在                           */
#define PAGE_RW        0x02   /* 可写 (0=只读)                    */
#define PAGE_USER      0x04   /* 用户态可访问 (0=仅内核)          */
#define PAGE_PWT       0x08   /* Write-Through 缓存               */
#define PAGE_PCD       0x10   /* Cache Disable                    */
#define PAGE_ACCESSED  0x20   /* 已访问 (CPU 自动设置)            */
#define PAGE_DIRTY     0x40   /* 已写 (CPU 自动设置)              */
#define PAGE_GLOBAL    0x100  /* 全局页 (TLB 不刷新)              */

/* ── API ── */

/* 加载 CR3 (切换地址空间) */
static inline void paging_load_cr3(uint32_t pd_phys) {
    __asm__ volatile ("movl %0, %%cr3" : : "r"(pd_phys));
}

/* 读取 CR3 */
static inline uint32_t paging_get_cr3() {
    uint32_t cr3;
    __asm__ volatile ("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* 启用分页 (CR0.PG=1) */
static inline void paging_enable() {
    uint32_t cr0;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;   /* CR0.PG = 1 */
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0));
}

/* 刷新单个 TLB 条目 */
static inline void tlb_flush_page(uint32_t vaddr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr));
}

/* 刷新整个 TLB (重新加载 CR3) */
static inline void tlb_flush_all() {
    uint32_t cr3 = paging_get_cr3();
    paging_load_cr3(cr3);
}

/* 检查分页是否启用 */
static inline int paging_is_enabled() {
    uint32_t cr0;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    return (cr0 >> 31) & 1;
}

#endif /* _KERNEL_ARCH_X86_PAGING_H */
