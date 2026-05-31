/* ============================================================================
 * kernel/arch/x86/boot.S — 内核入口 (汇编部分)
 *
 * 职责:
 *   1. Stage2 跳转到这里 (0x100000, 1MB)
 *   2. 设置内核栈
 *   3. 调用 C++ kernel_main()
 *   4. kernel_main 返回后停机
 *
 * 注意: 此时已在 32 位保护模式, GDT 已由 stage2 设置
 * ============================================================================ */

    .code32
    .section .text
    .globl _start

_start:
    /* ── 1. 设置内核栈 (栈放在 BSS 段后面) ── */
    movl    $__stack_top, %esp

    /* ── 2. 清零 BSS 段 ── */
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx
    shrl    $2, %ecx              /* 按 DWORD 计数 */
    xorl    %eax, %eax
    rep stosl

    /* ── 3. 调用 C++ 全局构造函数 (.init_array) ── */
    call    _init

    /* ── 4. 调用 C++ kernel_main() ── */
    call    kernel_main

    /* ── 5. kernel_main 返回 → 停机 ── */
    cli
    hlt
    jmp     .

/* ============================================================================
 * _init: 调用全局构造函数
 *
 * C++ 编译器将全局对象的构造函数指针放在 .init_array 段中。
 * 内核启动时必须调用它们。
 * ============================================================================ */
    .globl _init
_init:
    pushl   %ebp
    movl    %esp, %ebp
    pushl   %ebx

    movl    $__init_array_start, %ebx
    movl    $__init_array_end, %ecx
    cmpl    %ebx, %ecx
    je      2f

1:
    call    *(%ebx)               /* 调用构造函数 */
    addl    $4, %ebx
    cmpl    %ebx, %ecx
    jne     1b

2:
    popl    %ebx
    popl    %ebp
    ret
