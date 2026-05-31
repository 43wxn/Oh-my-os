/* ============================================================================
 * kernel/syscall/syscall.h — 系统调用定义
 * ============================================================================ */

#ifndef _KERNEL_SYSCALL_SYSCALL_H
#define _KERNEL_SYSCALL_SYSCALL_H

#include <stdint.h>

/* ── 系统调用号 ── */
#define SYS_GETPID   0
#define SYS_FORK     1
#define SYS_EXIT     2
#define SYS_WAIT     3
#define SYS_WRITE    4
#define SYS_NUM      5

/* ── 系统调用分发 (由 isr_syscall 汇编桩调用) ──
 * saved_regs 指向 pushal 帧的 EAX (8 个 uint32_t: eax,ecx,edx,ebx,esp,ebp,esi,edi)
 * 返回值写入 saved_regs[0] (即 pushal 的 EAX 槽) */
extern "C" void syscall_handler(uint32_t *saved_regs);

#endif /* _KERNEL_SYSCALL_SYSCALL_H */
