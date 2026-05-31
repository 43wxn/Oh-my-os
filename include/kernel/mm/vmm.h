/* ============================================================================
 * kernel/mm/vmm.h — 虚拟内存管理器
 *
 * 管理二级页表 (Page Directory + Page Tables)。
 * 提供页映射/解除映射, 以及地址空间切换。
 * ============================================================================ */

#ifndef _KERNEL_MM_VMM_H
#define _KERNEL_MM_VMM_H

#include <stdint.h>
#include "kernel/arch/x86/paging.h"

/* VMM 初始化: 创建初始页表, identity-map 内核空间, 启用分页 */
void vmm_init();

/* 映射虚拟页到物理页 */
int  vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);

/* 解除虚拟页映射, 返回原物理地址 */
uint32_t vmm_unmap_page(uint32_t vaddr);

/* 获取虚拟地址对应的物理地址 (遍历页表) */
uint32_t vmm_virt_to_phys(uint32_t vaddr);

/* 分配连续的虚拟地址空间 (不分配物理页) */
uint32_t vmm_alloc_vregion(uint32_t size_pages);

#endif /* _KERNEL_MM_VMM_H */
