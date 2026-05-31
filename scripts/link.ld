/* ============================================================================
 * scripts/link.ld — 内核链接脚本
 *
 * 内核被加载到 1MB (0x100000) 处。
 *
 * 布局:
 *   .text        — 代码段
 *   .rodata      — 只读数据
 *   .init_array  — C++ 全局构造函数表
 *   .data        — 已初始化数据
 *   .bss         — 未初始化数据 (启动时清零)
 *   .stack       — 内核栈
 * ============================================================================ */

ENTRY(_start)

KERNEL_BASE = 1M;

SECTIONS
{
    . = KERNEL_BASE;

    /* ---- 代码段 ---- */
    .text ALIGN(4K) : {
        *(.text*)
    }

    /* ---- 只读数据 ---- */
    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    /* ---- C++ 全局构造函数 ---- */
    .init_array ALIGN(4) : {
        __init_array_start = .;
        KEEP(*(.init_array*))
        KEEP(*(.ctors*))
        __init_array_end = .;
    }

    /* ---- 已初始化数据 ---- */
    .data ALIGN(4K) : {
        *(.data*)
    }

    /* ---- 未初始化数据 ---- */
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    /* ---- 内核堆起始 (紧接着 BSS) ---- */
    .heap ALIGN(4K) : {
        __heap_start = .;
    }

    /* ---- 内核栈 (16KB) ---- */
    .stack ALIGN(16) : {
        . = . + 16K;
        __stack_top = .;
    }

    /* 导出符号 */
    __kernel_end = .;
}
