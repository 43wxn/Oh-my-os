/* ============================================================================
 * kernel/syscall/syscall.cc — 系统调用分发
 *
 * ABI: eax=sysno, ebx=arg1, ecx=arg2, edx=arg3
 *      返回值通过 eax 传递
 *
 * saved_regs 布局 (pushal 顺序):
 *   [0]=edi  [1]=esi  [2]=ebp  [3]=orig_esp  [4]=ebx  [5]=edx  [6]=ecx  [7]=eax
 *
 * 注意 pushal 的实际顺序是 EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
 * 但不同汇编器可能不同。在 AT&T 语法中 pushal = pusha 顺序为:
 *   EAX(最先压), ECX, EDX, EBX, orig_ESP, EBP, ESI, EDI (最后压, 栈顶)
 * 所以 saved_regs[0] = EDI, saved_regs[7] = EAX
 * ============================================================================ */

#include "kernel/syscall/syscall.h"
#include "kernel/proc/proc.h"
#include "kernel/printk.h"

/* 前向声明 (实现在 proc.cc) */
extern int sys_fork(uint32_t *saved_regs);
extern void sys_exit(int status);
extern int sys_wait(int *user_status);

extern "C" void syscall_handler(uint32_t *saved_regs) {
    /* saved_regs 指向 pushal 帧底 (栈顶方向), 布局:
     *   [0]=edi, [1]=esi, [2]=ebp, [3]=orig_esp, [4]=ebx, [5]=edx, [6]=ecx, [7]=eax */
    uint32_t sysno = saved_regs[7];   /* EAX = syscall 号    */
    uint32_t arg1  = saved_regs[4];   /* EBX = arg1          */
    uint32_t arg2  = saved_regs[6];   /* ECX = arg2          */
    uint32_t arg3  = saved_regs[5];   /* EDX = arg3          */

    int result = -1;

    switch (sysno) {
    case SYS_GETPID: {
        pcb *cur = proc_current();
        result = cur ? (int)cur->pid : -1;
        break;
    }

    case SYS_FORK:
        result = sys_fork(saved_regs);
        break;

    case SYS_EXIT:
        sys_exit((int)arg1);
        /* sys_exit 不返回 */
        __builtin_unreachable();

    case SYS_WAIT:
        result = sys_wait((int *)arg1);
        break;

    case SYS_WRITE: {
        const char *str = (const char *)arg1;
        uint32_t len = arg2;
        for (uint32_t i = 0; i < len; i++) putchar(str[i]);
        result = (int)len;
        break;
    }

    default:
        printk("[SYSCALL] Unknown syscall %d\n", sysno);
        result = -1;
        break;
    }

    /* 返回值写入 pushal 的 EAX 槽 */
    saved_regs[7] = (uint32_t)result;
}
