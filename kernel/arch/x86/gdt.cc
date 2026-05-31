/* ============================================================================
 * kernel/arch/x86/gdt.cc — GDT + TSS 初始化
 *
 * 替换 stage2 的 3-段最小 GDT 为包含 Ring3 段 + TSS 的完整 GDT.
 * GDT 放在 kernel BSS 中，避免受 stage2 代码段空间限制.
 * ============================================================================ */

#include "kernel/arch/x86/gdt.h"
#include "kernel/printk.h"

/* ── 全局 GDT 表 (BSS, 6 个条目) ── */
static gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static gdtr      gdtr_reg;
static tss       tss __attribute__((aligned(16)));

/* ── 设置 GDT 条目 ── */
static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].base_low    = base & 0xFFFF;
    gdt[idx].base_mid    = (base >> 16) & 0xFF;
    gdt[idx].access      = access;
    gdt[idx].granularity = gran | ((limit >> 16) & 0x0F);
    gdt[idx].base_high   = (base >> 24) & 0xFF;
}

/* ── 初始化 GDT ── */
void gdt_init() {
    /* NULL (selector 0x00) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel Code (0x08): base=0, limit=4GB, DPL=0, exec/read, 32-bit */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);

    /* Kernel Data (0x10): base=0, limit=4GB, DPL=0, read/write, 32-bit */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);

    /* User Code (0x1B): base=0, limit=4GB, DPL=3, exec/read, 32-bit */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);

    /* User Data (0x23): base=0, limit=4GB, DPL=3, read/write, 32-bit */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);

    /* TSS 占位: 在 tss_init() 中填充 */
    gdt_set_entry(5, 0, 0, 0, 0);

    /* 加载新 GDT */
    gdtr_reg.limit = sizeof(gdt) - 1;
    gdtr_reg.base  = (uint32_t)&gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gdtr_reg));

    /* 长跳转刷新 CS, 然后重载全部数据段寄存器 */
    __asm__ volatile (
        "ljmp %[cs], $1f\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss"
        :
        : [cs] "i"(SEL_KCODE)
        : "ax"
    );

    printk("[GDT] Loaded %d entries (KCODE=0x%x, UCODE=0x%x, TSS=0x%x)\n",
           GDT_ENTRIES, SEL_KCODE, SEL_UCODE, SEL_TSS);
}

/* ── 初始化 TSS ── */
void tss_init() {
    /* 清零 TSS (已在 BSS 中, 但显式清零 iomap_base) */
    tss.ss0  = SEL_KDATA;
    tss.esp0 = 0;              /* 由调度器在切换时设置 */
    tss.iomap_base = sizeof(tss);  /* 无 I/O 权限位图 */

    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    /* TSS 描述符: 0x89 = Present|DPL0|TSS-available, 0x40 = 32-bit, limit hi=0 */
    gdt_set_entry(5, base, limit, 0x89, 0x00);

    /* 装载 Task Register */
    __asm__ volatile ("ltr %w0" : : "r"((uint16_t)SEL_TSS));

    printk("[TSS] Initialized: base=0x%x size=%d esp0=0x%x\n",
           base, sizeof(tss), tss.esp0);
}

/* ── 更新 Ring0 栈指针 (每次上下文切换时调用) ── */
void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}
