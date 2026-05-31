/* ============================================================================
 * user/init/init_prog.S — 第一个用户态测试程序 (Ring3)
 *
 * 测试: getpid → fork → parent wait / child exit
 *
 * 汇编为 flat binary, 由 kernel 嵌入并通过 proc_create_user 加载.
 * ============================================================================ */

    .code32
    .section .text
    .globl _start

_start:
    /* ── Test 1: getpid ── */
    movl    $0, %eax        /* SYS_GETPID */
    int     $0x80

    /* ── Test 2: fork ── */
    movl    $1, %eax        /* SYS_FORK */
    int     $0x80

    testl   %eax, %eax
    jnz     parent

    /* ── Child: exit(42) ── */
    movl    $2, %eax        /* SYS_EXIT */
    movl    $42, %ebx       /* status = 42 */
    int     $0x80
    /* 不应该到达这里 */

parent:
    /* ── Parent: wait(&status) ── */
    subl    $4, %esp        /* 在用户栈上分配 status 变量 */
    movl    %esp, %ebx      /* arg1 = &status */
    movl    $3, %eax        /* SYS_WAIT */
    int     $0x80
    /* eax = child PID, *status = 42 */

    /* ── exit(0) ── */
    movl    $2, %eax        /* SYS_EXIT */
    xorl    %ebx, %ebx      /* status = 0 */
    int     $0x80

    /* 不应该到达这里 */
    jmp     .
