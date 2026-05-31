/* ============================================================================
 * kernel/proc/proc.cc — 进程/线程调度器
 *
 * Round Robin 调度, 由 PIT 100Hz 时钟驱动.
 *
 * 上下文切换:
 *   switch_to(&old->ctx, &new->ctx)  → 保存 ESP/EBP/EBX/ESI/EDI
 *                                   → 切换栈, 恢复新线程的寄存器
 *                                   → ret 到新线程的代码
 *
 * 新线程 trampoline:
 * 
 *   switch_to 的 ret 跳到 proc_trampoline → 弹出 entry 函数地址
 *   → 调用 entry() → entry 返回时调 proc_exit()
 * ============================================================================ */

#include "kernel/proc/proc.h"
#include "kernel/mm/heap.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"

/* ── 汇编: 上下文切换 ── */
extern "C" void switch_to(cpu_context *prev_ctx, cpu_context *next_ctx);

/* ── 调度器状态 ── */
static pcb  proc_table[PROC_MAX];
static pcb *current     = nullptr;
static pcb *ready_head  = nullptr;
static int  next_pid    = 1;
static int  need_resched = 0;

/* ── 新线程退出钩子 ──
 * 新线程栈预设: [ebp=0] [ret=entry_fn] [ret2=proc_exit_hook]
 * switch_to 的 ret 跳到 entry_fn.
 * entry_fn 返回时 ret 跳到 proc_exit_hook → proc_exit(). */
extern "C" void proc_exit_hook() {
    proc_exit();
    __builtin_unreachable();
}

/* ── 初始化调度器 ── */
void proc_init() {
    for (int i = 0; i < PROC_MAX; i++) {
        proc_table[i].state = PROC_ZOMBIE;
        proc_table[i].stack = nullptr;
    }
    printk("[PROC] Scheduler initialized, max %d threads\n", PROC_MAX);
}

/* ── 创建内核线程 ── */
pcb* proc_create(void (*entry)()) {
    /* 找空闲 PCB */
    pcb *p = nullptr;
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].state == PROC_ZOMBIE) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p) {
        printk("[PROC] ERROR: No free PCB!\n");
        return nullptr;
    }

    /* 分配 PID 和内核栈 (先用静态 BSS 数组绕过 kmalloc/VMM) */
    p->pid   = next_pid++;

    static uint8_t stacks[PROC_MAX][PROC_STACK_SIZE] __attribute__((aligned(16)));
    p->stack = stacks[p->pid - 1];

    p->state = PROC_READY;
    p->cr3   = paging_get_cr3();

    /* 设置初始 CPU 上下文
     * switch_to 做: pop edi, esi, ebx, ebp; ret
     * 预设栈 (从上到下):
     *   sp → [edi=0] [esi=0] [ebx=0] [ebp=0]
     *         [ret=entry_fn]           ← switch_to ret 到这里
     *         [ret2=proc_exit_hook]    ← entry 返回后到这里 → proc_exit() */
    uint32_t *sp = (uint32_t *)(p->stack + PROC_STACK_SIZE);

    *--sp = (uint32_t)proc_exit_hook;        /* entry 返回后的 "return address" */
    *--sp = (uint32_t)entry;                 /* switch_to ret 的 "return address" */
    *--sp = 0;  /* ebp */
    *--sp = 0;  /* ebx */
    *--sp = 0;  /* esi */
    *--sp = 0;  /* edi */

    p->ctx.esp = (uint32_t)sp;
    p->ctx.ebp = 0;
    p->ctx.ebx = 0;
    p->ctx.esi = 0;
    p->ctx.edi = 0;

    /* 加入就绪队列 (链表头插) */
    p->next = ready_head;
    ready_head = p;

    printk("[PROC] Created thread PID=%d entry=0x%x stack=0x%x esp=0x%x\n",
           p->pid, (uint32_t)entry, (uint32_t)p->stack, p->ctx.esp);
    return p;
}

/* ── 主动让出 CPU ── */
void proc_yield() {
    __asm__ volatile ("cli");

    pcb *prev = current;
    pcb *next = ready_head;

    if (!next || next == prev) {
        __asm__ volatile ("sti");
        return;
    }

    /* 从就绪队列取出下一个 */
    ready_head = next->next;

    /* 当前线程回队尾 */
    if (prev) {
        prev->state = PROC_READY;
        prev->next  = nullptr;
        if (!ready_head) {
            ready_head = prev;
        } else {
            pcb *tail = ready_head;
            while (tail->next) tail = tail->next;
            tail->next = prev;
        }
    }

    next->state = PROC_RUNNING;
    current = next;

    switch_to(&prev->ctx, &next->ctx);
    __asm__ volatile ("sti");
}

/* ── 退出当前线程 ── */
__attribute__((noreturn))
void proc_exit() {
    __asm__ volatile ("cli");
    if (current) {
        printk("[PROC] Thread PID=%d exiting\n", current->pid);
        current->state = PROC_ZOMBIE;
        /* TODO: kfree(current->stack) when using kmalloc stacks */
        current->stack = nullptr;
        current = nullptr;
    }

    /* 调度到下一个线程, 不返回 */
    pcb *next = ready_head;
    if (next) {
        ready_head = next->next;
        next->state = PROC_RUNNING;
        current = next;

        /* 先把 next 上下文加载到寄存器, 再切栈.
         * 必须用 "r" 约束而非 "m", 否则切 ESP 后寻址失效. */
        uint32_t esp_ = next->ctx.esp;
        uint32_t ebp_ = next->ctx.ebp;
        uint32_t ebx_ = next->ctx.ebx;
        uint32_t esi_ = next->ctx.esi;
        uint32_t edi_ = next->ctx.edi;

        __asm__ volatile (
            "movl %0, %%esp\n\t"
            "movl %1, %%ebp\n\t"
            "movl %2, %%ebx\n\t"
            "movl %3, %%esi\n\t"
            "movl %4, %%edi\n\t"
            "ret"
            :
            : "r"(esp_), "r"(ebp_), "r"(ebx_), "r"(esi_), "r"(edi_)
            : "memory"
        );
        __builtin_unreachable();
    }

    /* 没有线程了, 停机 */
    printk("[PROC] No threads left, halting.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── 获取/设置当前 PCB ── */
pcb* proc_current() {
    return current;
}

void proc_set_current(pcb *p) {
    p->state = PROC_RUNNING;
    current = p;
}

/* ── PIT tick: 标记需要重调度 ── */
void proc_tick() {
    if (current) {
        need_resched = 1;
    }
}

/* ── 从中断返回前检查重调度标记 ──
 * 由 irq_common 在 IRQ 处理完成后调用.
 * 返回 1 表示已切换 (ESP 已变), 调用者应继续 iret. */
extern "C" int proc_schedule_check() {
    if (!need_resched || !current) {
        return 0;
    }
    need_resched = 0;

    pcb *next = ready_head;
    if (!next || next == current) {
        return 0;
    }

    /* 从就绪队列取下一个 */
    ready_head = next->next;

    /* 当前线程回队尾 */
    current->state = PROC_READY;
    current->next  = nullptr;
    if (!ready_head) {
        ready_head = current;
    } else {
        pcb *tail = ready_head;
        while (tail->next) tail = tail->next;
        tail->next = current;
    }

    /* 上下文切换 */
    pcb *prev = current;
    next->state = PROC_RUNNING;
    current = next;

    switch_to(&prev->ctx, &next->ctx);
    return 1;
}
