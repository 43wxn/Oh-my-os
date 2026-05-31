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
