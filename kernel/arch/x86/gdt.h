/* ============================================================================
 * kernel/arch/x86/gdt.h — GDT + TSS 定义
 *
 * 在 kernel 中构建新 GDT, 替换 stage2 的 3 段最小 GDT.
 * 新增: Ring3 代码/数据段 + TSS 描述符.
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_GDT_H
#define _KERNEL_ARCH_X86_GDT_H

#include <stdint.h>

/* ── GDT 选择子 ── */
#define SEL_NULL         0x00
#define SEL_KCODE        0x08   /* idx 1, RPL 0 */
#define SEL_KDATA        0x10   /* idx 2, RPL 0 */
#define SEL_UCODE        0x1B   /* idx 3, RPL 3 */
#define SEL_UDATA        0x23   /* idx 4, RPL 3 */
#define SEL_TSS          0x28   /* idx 5, RPL 0 */

#define GDT_ENTRIES      6

/* ── GDT 描述符 (8 字节) ── */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* ── GDTR (6 字节) ── */
struct gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* ── TSS (104 字节, 只填 esp0/ss0) ── */
struct tss {
    uint32_t link;          /* 0 */
    uint32_t esp0;          /* ring0 栈顶 */
    uint32_t ss0;           /* ring0 栈段 (KDATA) */
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;    /* = sizeof(tss) 表示无 I/O 位图 */
} __attribute__((packed));

/* ── API ── */
void gdt_init();                     /* 构建新 GDT, lgdt, 重载段寄存器 */
void tss_init();                     /* 初始化 TSS, ltr */
void tss_set_esp0(uint32_t esp0);    /* 更新 ring0 栈指针 */

#endif /* _KERNEL_ARCH_X86_GDT_H */
