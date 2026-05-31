/* ============================================================================
 * kernel/proc/proc.h — 进程/线程管理
 *
 * PCB: Process Control Block, 存储每个线程的 CPU 上下文和状态.
 * 调度: Round Robin, PIT 时钟驱动 (100Hz).
 *
 * cpu_context_t: switch_to 保存/恢复的寄存器集合.
 *   只保存 callee-saved 寄存器 (ESP/EBP/EBX/ESI/EDI).
 *   caller-saved (EAX/ECX/EDX) 由 C 编译器自动在栈上保存.
 * ============================================================================ */

#ifndef _KERNEL_PROC_PROC_H
#define _KERNEL_PROC_PROC_H

#include <stdint.h>

#define PROC_MAX        16
#define PROC_STACK_SIZE 4096        /* 每线程内核栈 4KB        */

/* 进程状态 */
enum proc_state {
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
};

/* CPU 上下文 (switch_to 交换的寄存器) */
struct cpu_context {
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
};

/* 进程控制块 */
struct pcb {
    uint32_t        pid;
    proc_state      state;
    cpu_context     ctx;            /* CPU 寄存器快照          */
    uint8_t        *stack;          /* 内核栈基址 (kmalloc)    */
    uint32_t        cr3;            /* 页目录物理地址          */
    pcb            *next;           /* 调度链表                */
};

/* API */
void proc_init();                        /* 初始化调度器            */
pcb* proc_create(void (*entry)());       /* 创建内核线程            */
void proc_yield();                       /* 主动让出 CPU            */
void proc_exit();                        /* 退出当前线程            */
pcb* proc_current();                     /* 获取当前 PCB            */
void proc_set_current(pcb *p);           /* 设置当前线程 (boot)      */

/* 由 PIT 中断调用: 标记需要重新调度 */
void proc_tick();

#endif /* _KERNEL_PROC_PROC_H */
