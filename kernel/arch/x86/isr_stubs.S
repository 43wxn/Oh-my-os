/* ============================================================================
 * kernel/arch/x86/isr_stubs.S — 中断/异常桩代码
 *
 * 每个中断/异常需要一个独立的入口桩。
 * 桩的作用: 压入中断号 → 跳到公共分发器 isr_handler() / irq_handler()
 *
 * 框架:
 *   1. 有错误码的异常: CPU 已压入 err_code → stub 只需压入 int_no
 *   2. 无错误码的异常/IRQ: CPU 未压入 err_code → stub 压入 0 再压入 int_no
 *   3. 公共入口保存全部寄存器 → 调 C++ handler → 恢复 → iret
 * ============================================================================ */

    .code32
    .section .text

/* ═══════════════════════════════════════════════════════════════════════════
 * 宏: 无错误码的中断桩
 * ═══════════════════════════════════════════════════════════════════════════ */
    .macro  ISR_NOERR  num
    .globl  isr\num
isr\num:
    pushl   $0                  /* 压入伪错误码 (保持帧格式统一)    */
    pushl   $\num               /* 压入中断号                       */
    jmp     isr_common
    .endm

/* ═══════════════════════════════════════════════════════════════════════════
 * 宏: 有错误码的异常桩 (CPU 已自动压入错误码)
 * ═══════════════════════════════════════════════════════════════════════════ */
    .macro  ISR_ERR  num
    .globl  isr\num
isr\num:
    pushl   $\num               /* 压入中断号 (错误码已由 CPU 压入) */
    jmp     isr_common
    .endm

/* ═══════════════════════════════════════════════════════════════════════════
 * 宏: IRQ 桩 (无错误码)
 * ═══════════════════════════════════════════════════════════════════════════ */
    .macro  IRQ  num, vector
    .globl  irq\num
irq\num:
    pushl   $0                  /* 压入伪错误码                     */
    pushl   $\vector            /* 压入中断号 (PIC重映射后 = 32+num) */
    jmp     irq_common
    .endm

/* ── CPU 异常 (0-31) ── */
ISR_NOERR   0      /* #DE  除零错误              */
ISR_NOERR   1      /* #DB  调试                   */
ISR_NOERR   2      /* #NMI 不可屏蔽中断           */
ISR_NOERR   3      /* #BP  断点                   */
ISR_NOERR   4      /* #OF  溢出                   */
ISR_NOERR   5      /* #BR  越界                   */
ISR_NOERR   6      /* #UD  无效操作码             */
ISR_NOERR   7      /* #NM  设备不可用 (FPU)       */
ISR_ERR     8      /* #DF  双重故障               */
ISR_NOERR   9      /* #MF  协处理器段越界         */
ISR_ERR     10     /* #TS  无效 TSS               */
ISR_ERR     11     /* #NP  段不存在               */
ISR_ERR     12     /* #SS  栈段错误               */
ISR_ERR     13     /* #GP  通用保护               */
ISR_ERR     14     /* #PF  缺页                   */
ISR_NOERR   15     /*      保留                   */
ISR_NOERR   16     /* #MF  x87 FPU 错误           */
ISR_ERR     17     /* #AC  对齐检查               */
ISR_NOERR   18     /* #MC  机器检查               */
ISR_NOERR   19     /* #XM  SIMD FPU 异常          */
ISR_NOERR   20     /* #VE  虚拟化异常             */
ISR_NOERR   21     /* #CP  控制保护               */
ISR_NOERR   22     /*      保留                   */
ISR_NOERR   23     /*      保留                   */
ISR_NOERR   24     /*      保留                   */
ISR_NOERR   25     /*      保留                   */
ISR_NOERR   26     /*      保留                   */
ISR_NOERR   27     /*      保留                   */
ISR_NOERR   28     /*      保留                   */
ISR_NOERR   29     /*      保留                   */
ISR_ERR     30     /* #SX  安全异常               */
ISR_NOERR   31     /*      保留                   */

/* ── IRQ 0-15 (PIC 重映射后向量 32-47) ── */
IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ  10, 42
IRQ  11, 43
IRQ  12, 44
IRQ  13, 45
IRQ  14, 46
IRQ  15, 47

/* ── 系统调用 (int 0x80) ── */
    .globl  isr_syscall
isr_syscall:
    pushl   $0
    pushl   $0x80
    jmp     isr_common

/* ═══════════════════════════════════════════════════════════════════════════
 * 公共 ISR 分发器
 *
 * 栈帧 (从上到下):
 *   [SS]         ← 仅特权级切换时存在 (Ring3→Ring0)
 *   [ESP]        ← 仅特权级切换时存在
 *   EFLAGS
 *   CS
 *   EIP
 *   int_no       ← 桩压入
 *   err_code     ← 桩或 CPU 压入
 *
 * 保存所有寄存器 → 调 C++ isr_handler() → 恢复 → iret
 * ═══════════════════════════════════════════════════════════════════════════ */

isr_common:
    /* 保存全部通用寄存器 */
    pushal                      /* EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI */
    pushw   %ds
    pushw   %es
    pushw   %fs
    pushw   %gs

    /* 设置内核数据段 */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    /* 传 int_frame 指针给 C++ handler
     * pushal(32B) + 4×pushw(8B) = 40B, ESP+40 = 原 ESP = &int_no */
    leal    40(%esp), %eax
    pushl   %eax
    call    isr_handler
    addl    $4, %esp

    /* 恢复段寄存器 */
    popw    %gs
    popw    %fs
    popw    %es
    popw    %ds

    /* 恢复通用寄存器 */
    popal

    /* 跳过 int_no 和 err_code */
    addl    $8, %esp

    /* 返回 */
    iret

/* ═══════════════════════════════════════════════════════════════════════════
 * 公共 IRQ 分发器
 *
 * 与 isr_common 基本相同, 但调用 irq_handler(),
 * 并且返回前发 EOI (End Of Interrupt) 给 PIC。
 * ═══════════════════════════════════════════════════════════════════════════ */

irq_common:
    /* 保存全部寄存器 */
    pushal
    pushw   %ds
    pushw   %es
    pushw   %fs
    pushw   %gs

    /* 设置内核数据段 */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    /* 传 int_frame 指针: ESP+40 = 原 ESP = &int_no */
    leal    40(%esp), %eax
    pushl   %eax
    call    irq_handler
    addl    $4, %esp

    /* ── 调度检查: IRQ 处理完后可能触发上下文切换 ── */
    call    proc_schedule_check
    testl   %eax, %eax
    jnz     1f              /* 已切换, ESP 指向新线程栈, 继续恢复 */
    /* 未切换: 恢复当前线程 */
1:
    /* 恢复段寄存器 */
    popw    %gs
    popw    %fs
    popw    %es
    popw    %ds

    /* 恢复通用寄存器 */
    popal

    /* 跳过 int_no 和 err_code */
    addl    $8, %esp

    /* 返回 */
    iret
