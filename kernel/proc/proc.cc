/* ============================================================================
 * kernel/proc/proc.cc — 进程/线程调度器 + 用户态进程
 *
 * Round Robin 调度, PIT 100Hz 抢占.
 *
 * 上下文切换:
 *   switch_to(&old->ctx, &new->ctx)
 *     → 保存 ESP/EBP/EBX/ESI/EDI 到 old
 *     → 从 new 恢复 → ret 到新线程
 *
 * 内核线程栈布局 (proc_create):
 *   sp→[edi=0][esi=0][ebx=0][ebp=0]
 *       [ret=entry_fn]              ← switch_to ret 到这里
 *       [ret2=proc_exit_hook]       ← entry 返回后到这里
 *
 * 用户进程栈布局 (proc_create_user):
 *   sp→[edi=0][esi=0][ebx=0][ebp=0]
 *       [ret=ring3_trampoline]      ← switch_to ret 到这里
 *       [EIP=entry][CS=UCODE|3][EFLAGS=0x202][ESP=stack_top][SS=UDATA|3]
 *       ← ring3_trampoline 执行 iret, CPU 弹出这些进入 Ring3
 * ============================================================================ */

#include "kernel/proc/proc.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/arch/x86/gdt.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/printk.h"
#include "kernel/syscall/syscall.h"

/* ── 汇编 ── */
extern "C" void switch_to(cpu_context *prev_ctx, cpu_context *next_ctx);
extern "C" void ring3_trampoline();
extern "C" void fork_trampoline();
extern "C" void proc_exit_hook();

/* ── 调度器全局状态 ── */
static pcb  proc_table[PROC_MAX];
static pcb *current     = nullptr;
static pcb *ready_head  = nullptr;
static int  next_pid    = 1;
static int  need_resched = 0;

/* ── 用户进程内核栈 (静态 BSS, 绕过 kmalloc/VMM bug) ── */
static uint8_t kstacks[PROC_MAX][PROC_STACK_SIZE] __attribute__((aligned(16)));
static bool    kstack_used[PROC_MAX];

/* ── 辅助: 字符串复制 ── */
static void memcpy_w(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

/* ── 辅助: 内核栈分配/释放 ── */
static uint8_t* kstack_alloc() {
    for (int i = 0; i < PROC_MAX; i++) {
        if (!kstack_used[i]) {
            kstack_used[i] = true;
            return kstacks[i];
        }
    }
    return nullptr;
}
static void kstack_free(uint8_t *s) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (kstacks[i] == s) { kstack_used[i] = false; return; }
    }
}

/* ── 辅助: 找空闲 PCB ── */
static pcb* find_free_pcb() {
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].state == PROC_ZOMBIE && proc_table[i].magic != PCB_MAGIC)
            return &proc_table[i];
    }
    return nullptr;
}

/* ── 辅助: 切换前准备 (TSS.esp0 + CR3) ── */
static void switch_prepare(pcb *next) {
    tss_set_esp0((uint32_t)next->stack + PROC_STACK_SIZE);
    /* 注意: 当前所有进程共享内核页目录 (PDE[0] 身份映射).
     * 用户态进程有独立页目录, 需要在切换 CR3 前确认.
     * 如果 next->cr3 != 当前 cr3, 需要先切换 cr3.
     * 但当前 VMM 的 current_pd_phys 是全局的, 我们先不切换 cr3,
     * 而是通过 vmm_switch_pd() 来切换. */
    uint32_t cur_cr3 = paging_get_cr3();
    if (next->cr3 != cur_cr3 && next->cr3 != 0) {
        vmm_switch_pd(next->cr3);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 初始化 / 内核线程
 * ═══════════════════════════════════════════════════════════════════════════ */

void proc_init() {
    for (int i = 0; i < PROC_MAX; i++) {
        proc_table[i].state      = PROC_ZOMBIE;
        proc_table[i].stack      = nullptr;
        proc_table[i].magic      = 0;
        proc_table[i].parent_pid = 0;
    }
    for (int i = 0; i < PROC_MAX; i++) kstack_used[i] = false;
    printk("[PROC] Scheduler initialized, max %d threads\n", PROC_MAX);
}

pcb* proc_create(void (*entry)()) {
    pcb *p = find_free_pcb();
    if (!p) { printk("[PROC] ERROR: No free PCB!\n"); return nullptr; }

    p->pid   = next_pid++;
    p->stack = kstack_alloc();
    if (!p->stack) { printk("[PROC] ERROR: No free kernel stack!\n"); return nullptr; }

    p->state      = PROC_READY;
    p->cr3        = paging_get_cr3();
    p->parent_pid = 0;
    p->magic      = PCB_MAGIC;

    /* 预设内核栈 (从上到下) */
    uint32_t *sp = (uint32_t *)(p->stack + PROC_STACK_SIZE);
    *--sp = (uint32_t)proc_exit_hook;       /* entry 返回后的 ret 地址  */
    *--sp = (uint32_t)entry;                /* switch_to ret 的地址      */
    *--sp = 0;  /* ebp */
    *--sp = 0;  /* ebx */
    *--sp = 0;  /* esi */
    *--sp = 0;  /* edi */

    p->ctx.esp = (uint32_t)sp + 4 * 4;     /* 指向 entry_fn 上方        */
    p->ctx.ebp = 0;
    p->ctx.ebx = 0;
    p->ctx.esi = 0;
    p->ctx.edi = 0;

    /* 加入就绪队列 */
    p->next = ready_head;
    ready_head = p;

    printk("[PROC] Created kthread PID=%d entry=0x%x esp=0x%x\n",
           p->pid, (uint32_t)entry, p->ctx.esp);
    return p;
}

/* ── 内核线程退出钩子 ── */
extern "C" void proc_exit_hook() {
    proc_exit();
    __builtin_unreachable();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 用户进程创建
 * ═══════════════════════════════════════════════════════════════════════════ */

pcb* proc_create_user(const uint8_t *code, uint32_t code_size,
                      uint32_t entry_vaddr, uint32_t stack_vaddr) {
    pcb *p = find_free_pcb();
    if (!p) { printk("[PROC] ERROR: No free PCB for user proc!\n"); return nullptr; }

    p->pid   = next_pid++;
    p->stack = kstack_alloc();
    if (!p->stack) { printk("[PROC] ERROR: No free kernel stack!\n"); return nullptr; }

    p->state      = PROC_READY;
    p->parent_pid = 0;
    p->magic      = PCB_MAGIC;

    /* ── 创建新页目录 ── */
    uint32_t *new_pd = (uint32_t *)pmm_alloc_page();
    if (!new_pd) { printk("[PROC] ERROR: OOM for page directory!\n"); return nullptr; }
    p->cr3 = (uint32_t)new_pd;

    /* 清零新页目录 */
    for (int i = 0; i < 1024; i++) new_pd[i] = 0;

    /* 复制内核 PDE[0] (身份映射 0-4MB, 不含 PAGE_USER) */
    uint32_t *cur_pd = (uint32_t *)paging_get_cr3();
    new_pd[0] = cur_pd[0];

    /* ── 映射用户代码页 (切换到新 PD, vmm 才能写对页表) ── */
    uint32_t old_pd = paging_get_cr3();
    vmm_switch_pd(p->cr3);

    uint32_t code_pages = (code_size + 0xFFF) / 0x1000;
    for (uint32_t i = 0; i < code_pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) { printk("[PROC] OOM for code page!\n"); return nullptr; }
        uint32_t vaddr = entry_vaddr + i * 0x1000;
        uint32_t copy_len = (i == code_pages - 1 && (code_size & 0xFFF))
                            ? (code_size & 0xFFF) : 0x1000;
        vmm_map_page(vaddr, (uint32_t)phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        memcpy_w((void *)vaddr, code + i * 0x1000, copy_len);
    }

    /* ── 映射用户栈 (1 页, 4KB) ── */
    void *stack_phys = pmm_alloc_page();
    if (!stack_phys) { printk("[PROC] OOM for user stack!\n"); return nullptr; }
    vmm_map_page(stack_vaddr & 0xFFFFF000, (uint32_t)stack_phys,
                 PAGE_PRESENT | PAGE_RW | PAGE_USER);

    /* 恢复内核 PD */
    vmm_switch_pd(old_pd);

    /* ── 预设内核栈上的 iret 帧 ── */
    uint32_t *sp = (uint32_t *)(p->stack + PROC_STACK_SIZE);

    /* iret 帧: CPU 从 ring3_trampoline 的 iret 弹出 */
    *--sp = (SEL_UDATA | 3);                 /* SS                      */
    *--sp = stack_vaddr;                      /* ESP (用户栈顶)          */
    *--sp = 0x202;                            /* EFLAGS (IF=1)           */
    *--sp = (SEL_UCODE | 3);                  /* CS                      */
    *--sp = entry_vaddr;                      /* EIP                     */

    /* ring3_trampoline 的 "返回地址" (switch_to ret 到这里) */
    *--sp = (uint32_t)ring3_trampoline;

    /* callee-saved 寄存器 (由 trampoline 忽略, 但要占位) */
    *--sp = 0;  /* ebp */
    *--sp = 0;  /* ebx */
    *--sp = 0;  /* esi */
    *--sp = 0;  /* edi */

    p->ctx.esp = (uint32_t)sp + 4 * 4;         /* 指向 ring3_trampoline   */
    p->ctx.ebp = 0;
    p->ctx.ebx = 0;
    p->ctx.esi = 0;
    p->ctx.edi = 0;

    /* 加入就绪队列 */
    p->next = ready_head;
    ready_head = p;

    printk("[PROC] User proc PID=%d cr3=0x%x entry=0x%x stack=0x%x\n",
           p->pid, p->cr3, entry_vaddr, stack_vaddr);
    return p;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 调度
 * ═══════════════════════════════════════════════════════════════════════════ */

void proc_yield() {
    __asm__ volatile ("cli");

    pcb *prev = current;
    pcb *next = ready_head;

    if (!next || next == prev) {
        __asm__ volatile ("sti");
        return;
    }

    ready_head = next->next;

    /* 当前回队尾 */
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

    switch_prepare(next);
    switch_to(&prev->ctx, &next->ctx);
    __asm__ volatile ("sti");
}

void proc_exit() {
    __asm__ volatile ("cli");
    if (current) {
        printk("[PROC] Thread PID=%d exiting\n", current->pid);
        current->state = PROC_ZOMBIE;
        kstack_free(current->stack);
        current->stack = nullptr;
        current = nullptr;
    }

    /* 调度下一个, 不返回 */
    pcb *next = ready_head;
    if (next) {
        ready_head = next->next;
        next->state = PROC_RUNNING;
        current = next;

        switch_prepare(next);

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

    printk("[PROC] No threads left, halting.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── 带状态码退出 (用户态 exit syscall) ── */
void proc_exit_with_status(int status) {
    __asm__ volatile ("cli");
    if (current) {
        printk("[PROC] PID=%d exit(%d)\n", current->pid, status);
        current->state = PROC_ZOMBIE;
        current->exit_status = status;

        /* 唤醒阻塞在 wait() 中的父进程 */
        if (current->parent_pid != 0) {
            for (int i = 0; i < PROC_MAX; i++) {
                pcb *parent = &proc_table[i];
                if (parent->magic == PCB_MAGIC &&
                    parent->pid == current->parent_pid &&
                    parent->state == PROC_BLOCKED) {
                    parent->state = PROC_READY;
                    parent->next = ready_head;
                    ready_head = parent;
                    printk("[PROC] Woke parent PID=%d\n", parent->pid);
                    break;
                }
            }
        }

        kstack_free(current->stack);
        current->stack = nullptr;
        current = nullptr;
    }

    /* 调度下一个 */
    pcb *next = ready_head;
    if (next) {
        ready_head = next->next;
        next->state = PROC_RUNNING;
        current = next;

        switch_prepare(next);

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

    printk("[PROC] No threads left, halting.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

pcb* proc_current() { return current; }

void proc_set_current(pcb *p) {
    p->state = PROC_RUNNING;
    current = p;
}

void proc_tick() {
    if (current) need_resched = 1;
}

extern "C" int proc_schedule_check() {
    if (!need_resched || !current) return 0;
    need_resched = 0;

    pcb *next = ready_head;
    if (!next || next == current) return 0;

    ready_head = next->next;

    current->state = PROC_READY;
    current->next  = nullptr;
    if (!ready_head) {
        ready_head = current;
    } else {
        pcb *tail = ready_head;
        while (tail->next) tail = tail->next;
        tail->next = current;
    }

    pcb *prev = current;
    next->state = PROC_RUNNING;
    current = next;

    switch_prepare(next);
    switch_to(&prev->ctx, &next->ctx);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 系统调用: fork / exit / wait
 * ═══════════════════════════════════════════════════════════════════════════ */

int sys_fork(uint32_t *saved_regs) {
    pcb *parent = current;
    if (!parent) return -1;

    /* 1. 分配子 PCB + 内核栈 */
    pcb *child = find_free_pcb();
    if (!child) return -1;

    child->pid   = next_pid++;
    child->stack = kstack_alloc();
    if (!child->stack) { child->state = PROC_ZOMBIE; return -1; }

    child->state      = PROC_READY;
    child->parent_pid = parent->pid;
    child->magic      = PCB_MAGIC;

    /* 2. 复制页目录 (内核 PDE 共享, 用户 PDE 深拷贝) */
    uint32_t *parent_pd = (uint32_t *)parent->cr3;
    uint32_t *child_pd  = (uint32_t *)pmm_alloc_page();
    if (!child_pd) { child->state = PROC_ZOMBIE; return -1; }
    child->cr3 = (uint32_t)child_pd;

    for (int i = 0; i < 1024; i++) {
        uint32_t pde = parent_pd[i];
        if (!(pde & PAGE_PRESENT)) { child_pd[i] = 0; continue; }
        if (!(pde & PAGE_USER))   { child_pd[i] = pde; continue; }

        uint32_t *parent_pt = (uint32_t *)(pde & 0xFFFFF000);
        uint32_t *child_pt  = (uint32_t *)pmm_alloc_page();
        if (!child_pt) { child->state = PROC_ZOMBIE; return -1; }

        for (int j = 0; j < 1024; j++) child_pt[j] = 0;
        child_pd[i] = ((uint32_t)child_pt) | (pde & 0xFFF);

        for (int j = 0; j < 1024; j++) {
            uint32_t pte = parent_pt[j];
            if (!(pte & PAGE_PRESENT)) continue;
            void *new_phys = pmm_alloc_page();
            if (!new_phys) { child->state = PROC_ZOMBIE; return -1; }
            memcpy_w(new_phys, (void *)(pte & 0xFFFFF000), 0x1000);
            child_pt[j] = ((uint32_t)new_phys) | (pte & 0xFFF);
        }
    }

    /* 3. 构建子进程内核栈: 复制 iret 帧 + 新建 pushal (EAX=0) + fork_trampoline
     *
     * 父进程内核栈布局 (从高到低, syscall 入口):
     *   [SS] [ESP_user] [EFLAGS] [CS] [EIP]                    ← iret 帧
     *   [EAX][ECX][EDX][EBX][orig_ESP][EBP][ESI][EDI]          ← pushal (saved_regs 指向此处)
     *   [GS][FS][ES][DS]                                        ← pushw
     *   [ret_to_asm]                                            ← call syscall_handler
     *
     * 子进程内核栈:
     *   [SS] [ESP_user] [EFLAGS] [CS] [EIP]                    ← 从父进程复制
     *   [EAX=0][ECX][EDX][EBX][orig_ESP][EBP][ESI][EDI]        ← 复制, EAX 改 0
     *   [fork_trampoline]                                       ← switch_to ret 到这里
     */

    /* saved_regs 是父进程 pushal 帧指针, 位于 iret 帧下方 */
    /* iret 帧就在 saved_regs 上方: saved_regs[8..12] (越过 pushal 的 8 个寄存器) */
    uint32_t *parent_iret = &saved_regs[8];  /* iret 帧: [EIP][CS][EFLAGS][ESP][SS] */

    uint32_t *sp = (uint32_t *)(child->stack + PROC_STACK_SIZE);

    /* ── 压入 iret 帧 ── */
    *--sp = parent_iret[4];     /* SS */
    *--sp = parent_iret[3];     /* ESP (user) */
    *--sp = parent_iret[2];     /* EFLAGS */
    *--sp = parent_iret[1];     /* CS */
    *--sp = parent_iret[0];     /* EIP (fork 返回后的指令) */

    /* ── 压入 pushal 帧 (子进程 EAX=0) ── */
    *--sp = 0;                  /* EAX = 0 → 子进程 fork 返回值 */
    *--sp = saved_regs[6];      /* ECX */
    *--sp = saved_regs[5];      /* EDX */
    *--sp = saved_regs[4];      /* EBX */
    *--sp = saved_regs[3];      /* orig_ESP (子进程用新的) */
    *--sp = saved_regs[2];      /* EBP */
    *--sp = saved_regs[1];      /* ESI */
    *--sp = saved_regs[0];      /* EDI */

    /* ── 压入 fork_trampoline 返回地址 ── */
    *--sp = (uint32_t)fork_trampoline;

    /* ── 设置子进程 ctx: esp 指向 fork_trampoline ── */
    child->ctx.esp = (uint32_t)sp;
    child->ctx.ebp = 0;
    child->ctx.ebx = 0;
    child->ctx.esi = 0;
    child->ctx.edi = 0;

    /* 4. 入队 */
    child->next = ready_head;
    ready_head = child;

    printk("[FORK] Parent PID=%d → Child PID=%d cr3=0x%x esp=0x%x\n",
           parent->pid, child->pid, child->cr3, child->ctx.esp);
    return child->pid;
}

void sys_exit(int status) {
    proc_exit_with_status(status);
}

int sys_wait(int *user_status) {
    pcb *p = current;
    if (!p) return -1;

    for (;;) {
        pcb *found = nullptr;
        int has_children = 0;

        for (int i = 0; i < PROC_MAX; i++) {
            pcb *child = &proc_table[i];
            if (child->magic != PCB_MAGIC) continue;
            if (child->parent_pid != p->pid) continue;

            has_children = 1;
            if (child->state == PROC_ZOMBIE) { found = child; break; }
        }

        if (!has_children) return -1;

        if (found) {
            int cpid  = found->pid;
            int st    = found->exit_status;

            if (user_status) {
                *user_status = st;  /* 写回用户空间 */
            }

            /* 释放子进程资源 (PCB 标记为可复用, 栈和页表后续处理) */
            found->magic = 0;
            found->state = PROC_ZOMBIE;
            found->pid   = 0;
            kstack_free(found->stack);
            found->stack = nullptr;

            printk("[WAIT] PID=%d collected child PID=%d status=%d\n",
                   p->pid, cpid, st);
            return cpid;
        }

        /* 没有 ZOMBIE 子进程, 阻塞当前进程 */
        printk("[WAIT] PID=%d blocking (no exited children)\n", p->pid);
        __asm__ volatile ("cli");
        p->state = PROC_BLOCKED;

        pcb *next = ready_head;
        if (!next) {
            printk("[WAIT] No runnable threads, deadlock!\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }

        ready_head = next->next;
        next->state = PROC_RUNNING;
        current = next;

        switch_prepare(next);
        switch_to(&p->ctx, &next->ctx);
        __asm__ volatile ("sti");

        /* 被唤醒后回到这里, 重新检查子进程 */
    }
}
