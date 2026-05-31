/* ============================================================================
 * kernel/arch/x86/switch.S — CPU 上下文切换
 *
 * void switch_to(cpu_context_t *old, cpu_context_t *new)
 *
 * 保存当前 ESP/EBP/EBX/ESI/EDI 到 old, 从 new 恢复.
 * ret 到新线程的返回地址.
 * ============================================================================ */

    .code32
    .section .text
    .globl switch_to

switch_to:
    /* 保存当前上下文到 old */
    movl    4(%esp), %eax           /* eax = old ctx ptr       */
    movl    %esp,  0(%eax)          /* old->esp                */
    movl    %ebp,  4(%eax)          /* old->ebp                */
    movl    %ebx,  8(%eax)          /* old->ebx                */
    movl    %esi, 12(%eax)          /* old->esi                */
    movl    %edi, 16(%eax)          /* old->edi                */

    /* 从 new 恢复上下文 */
    movl    8(%esp), %edx           /* edx = new ctx ptr       */
    movl    0(%edx), %esp           /* new->esp                */
    movl    4(%edx), %ebp           /* new->ebp                */
    movl    8(%edx), %ebx           /* new->ebx                */
    movl    12(%edx), %esi          /* new->esi                */
    movl    16(%edx), %edi          /* new->edi                */

    ret                             /* 跳到新线程的返回地址     */

/* ═══════════════════════════════════════════════════════════════════════════
 * ring3_trampoline — 首次进入 Ring3 的跳板
 *
 * 新用户进程的栈上预设了 iret 帧 (SS,ESP,EFLAGS,CS,EIP).
 * switch_to 的 ret 跳到这里 → 设用户数据段 → iret 进入 Ring3.
 * ═══════════════════════════════════════════════════════════════════════════ */
    .globl ring3_trampoline
ring3_trampoline:
    movw    $0x23, %ax          /* SEL_UDATA | RPL=3             */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    /* 此时栈顶是 iret 帧: [EIP][CS][EFLAGS][ESP][SS]            */
    iret

/* ═══════════════════════════════════════════════════════════════════════════
 * fork_trampoline — fork 子进程的首次执行入口
 *
 * 子进程内核栈预设:
 *   [SS][ESP][EFLAGS][CS][EIP]          ← iret 帧 (复制自父进程)
 *   [EAX=0][ECX][EDX][EBX][ESP][EBP][ESI][EDI]  ← pushal 帧
 *   [ret=fork_trampoline]               ← switch_to ret 到这里
 *
 * fork_trampoline 设用户数据段 → pop pushal (EAX=0→子进程返回值)
 * → iret 到 Ring3 (EIP = fork 返回后的指令).
 * ═══════════════════════════════════════════════════════════════════════════ */
    .globl fork_trampoline
fork_trampoline:
    movw    $0x23, %ax          /* SEL_UDATA | RPL=3             */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    /* 弹出 pushal 帧: EDI, ESI, EBP, EBX(orig_ESP), EBX, EDX, ECX, EAX */
    popl    %edi
    popl    %esi
    popl    %ebp
    addl    $4, %esp            /* 跳过 orig_ESP                 */
    popl    %ebx
    popl    %edx
    popl    %ecx
    popl    %eax                /* EAX = 0 (子进程 fork 返回值)  */
    /* 栈顶现在是 iret 帧                                         */
    iret
