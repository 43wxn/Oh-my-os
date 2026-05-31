/* ============================================================================
 * kernel/arch/x86/idt.h — 中断描述符表
 * ============================================================================ */

#ifndef _KERNEL_ARCH_X86_IDT_H
#define _KERNEL_ARCH_X86_IDT_H

#include <stdint.h>

/* 中断向量总数 */
#define IDT_ENTRIES 256

/* 中断门标志 */
#define IDT_FLAG_PRESENT   0x80
#define IDT_FLAG_DPL0      0x00
#define IDT_FLAG_DPL3      0x60
#define IDT_FLAG_INT_GATE  0x0E  /* 中断门 (关中断)      */
#define IDT_FLAG_TRAP_GATE 0x0F  /* 陷阱门 (不关中断)    */

/* IDT 描述符 (8 bytes) */
struct idt_entry_t {
    uint16_t base_low;         /* 处理函数地址 [15:0]    */
    uint16_t selector;         /* 代码段选择子 (0x08)     */
    uint8_t  zero;             /* 保留, 必须为 0           */
    uint8_t  flags;            /* P|DPL|0|GateType       */
    uint16_t base_high;        /* 处理函数地址 [31:16]   */
} __attribute__((packed));

/* IDTR (6 bytes) */
struct idtr_t {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* 中断栈帧 (CPU + stub 推送)
 *
 * 对于有错误码的异常 (如 #GP, #PF):
 *   CPU 自动压入: SS, ESP, EFLAGS, CS, EIP, ErrorCode
 *   stub 压入:    IntNo
 *
 * 对于无错误码的异常和 IRQ:
 *   CPU 自动压入: SS, ESP, EFLAGS, CS, EIP
 *   stub 压入:    ErrorCode(0), IntNo
 */
struct int_frame_t {
    /* 由汇编 stub 压入 */
    uint32_t int_no;           /* 中断向量号              */
    uint32_t err_code;         /* 错误码 (无则为 0)       */

    /* 由 CPU 自动压入 */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;              /* 仅当特权级切换时有效     */
    uint32_t ss;               /* 仅当特权级切换时有效     */
} __attribute__((packed));

/* API */
void idt_init();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

#endif /* _KERNEL_ARCH_X86_IDT_H */
