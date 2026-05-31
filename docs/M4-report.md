# Oh-my-os M4 阶段报告：进程管理

## 阶段目标

实现完整的多任务支持：内核线程调度、用户态进程 (Ring3)、fork/exit/wait 系统调用。

## 架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                       Shell (kernel.cc)                       │
│     > help / clear / info / echo / m4test / user / crash      │
├──────────────────────────────────────────────────────────────┤
│  系统调用层 (kernel/syscall/syscall.cc)                        │
│    int 0x80 → eax=sysno, ebx/ecx/edx=arg → dispatch           │
│    SYS_GETPID(0)  SYS_FORK(1)  SYS_EXIT(2)  SYS_WAIT(3)      │
├──────────────────────────────────────────────────────────────┤
│  调度器 (kernel/proc/proc.cc)                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                    │
│  │  PCB A   │  │  PCB B   │  │  PCB C   │  ... (16)          │
│  │  kernel  │  │  user    │  │  user    │                    │
│  │  thread  │  │  proc    │  │  proc    │                    │
│  │  cr3=K   │  │  cr3=U_A │  │  cr3=U_B │                    │
│  └──────────┘  └──────────┘  └──────────┘                    │
│       Round-Robin 环形就绪队列                                  │
├──────────────────────────────────────────────────────────────┤
│  上下文切换 (kernel/arch/x86/switch.S)                          │
│    switch_to(old, new): 保存/恢复 ESP/EBP/EBX/ESI/EDI          │
│    ring3_trampoline: 设用户DS → iret → Ring3                   │
│    fork_trampoline:  设用户DS → pop pushal → iret              │
├──────────────────────────────────────────────────────────────┤
│  硬件层                                                        │
│    GDT(6段): NULL KCODE KDATA UCODE UDATA TSS                  │
│    TSS.esp0: 每次上下文切换前更新                                │
│    PIT 100Hz → proc_tick() → proc_schedule_check() 抢占         │
│    int 0x80 DPL=3 陷阱门 → isr_syscall → syscall_handler       │
└──────────────────────────────────────────────────────────────┘
```

## 核心数据结构

### cpu_context (5 个 callee-saved 寄存器)

```c
struct cpu_context {
    uint32_t esp, ebp, ebx, esi, edi;
};
```

### PCB (进程控制块)

```c
struct pcb {
    uint32_t    pid;
    proc_state  state;       // READY / RUNNING / BLOCKED / ZOMBIE
    cpu_context ctx;         // 寄存器快照
    uint8_t    *stack;       // 内核栈基址
    uint32_t    cr3;         // 页目录物理地址
    pcb        *next;        // 就绪队列链表
    uint32_t    parent_pid;  // 父进程 PID (0=内核线程)
    int         exit_status; // 退出状态码
    uint32_t    magic;       // PCB_MAGIC (0x5043424D)
};
```

## Part 1: 内核线程调度 (Commit: `1bc51cb`)

| 函数 | 功能 |
|------|------|
| `proc_init()` | 初始化 PCB 表 |
| `proc_create(entry)` | 创建内核线程, 预设栈+上下文 |
| `proc_yield()` | 协作式让出 CPU |
| `proc_exit()` | 退出并调度下一个线程 |
| `proc_tick()` | PIT 回调, 标记需要抢占 |
| `proc_schedule_check()` | IRQ 返回前抢占检查 |

### 调度流程

1. **协作式** (`proc_yield`): cli → 取队头 → 当前回队尾 → switch_to → sti
2. **抢占式** (PIT 100Hz): IRQ0 → proc_tick → 置 need_resched → proc_schedule_check → switch_to → iret

### 内核线程栈布局

```
sp → [edi=0][esi=0][ebx=0][ebp=0]
      [ret=entry_fn]        ← switch_to ret 到这里
      [ret2=proc_exit_hook] ← entry 返回后调用 proc_exit()
```

## Part 2: 用户态进程 + 系统调用 (Commit: `c555a20`)

### GDT + TSS

- 替换 stage2 的 3 段 GDT 为 6 段 (kernel BSS 中构建)
- **KCODE (0x08)**: DPL=0, 代码, 4GB
- **KDATA (0x10)**: DPL=0, 数据, 4GB
- **UCODE (0x1B)**: DPL=3, 代码, 4GB → Ring3 用户代码段
- **UDATA (0x23)**: DPL=3, 数据, 4GB → Ring3 用户数据段
- **TSS (0x28)**: DPL=0, 32-bit TSS → `ltr` 装载
- `tss.esp0 = next->stack + PROC_STACK_SIZE` — 每次上下文切换前更新

### 系统调用 ABI

```
eax = syscall number
ebx = arg1, ecx = arg2, edx = arg3
返回值 → eax
```

`int 0x80` → `isr_syscall` (pushal+设段) → `syscall_handler(saved_regs)` → 写返回值到 pushal 的 eax 槽 → popal → iret

| 调用号 | 函数 | 功能 |
|--------|------|------|
| 0 | SYS_GETPID | 返回当前进程 PID |
| 1 | SYS_FORK | 复制当前进程, 子进程返回 0 |
| 2 | SYS_EXIT | 退出并通知父进程 |
| 3 | SYS_WAIT | 等待子进程退出, 收集状态码 |
| 4 | SYS_WRITE | 调试输出 (打印 len 字节到 VGA) |

### 用户进程创建

`proc_create_user(code, size, entry_vaddr, stack_vaddr)`:
1. 分配 PCB + 内核栈
2. 创建新页目录: 复制 PDE[0] (内核 0-4MB 身份映射), 其余清零
3. 用 `vmm_switch_pd` 切换到新 PD, 映射用户代码页 (PAGE_USER) 并复制代码
4. 映射用户栈页 (PAGE_USER)
5. 切换回内核 PD
6. 预设内核栈上的 iret 帧 + ring3_trampoline 返回地址

### 用户进程栈布局 (首次进入)

```
sp → [edi=0][esi=0][ebx=0][ebp=0]
      [ret=ring3_trampoline]   ← switch_to ret 到这里
      [EIP=entry][CS=0x1B][EFLAGS=0x202][ESP=stack_top][SS=0x23]
      ← ring3_trampoline: movw $0x23; iret → Ring3
```

### fork() 实现

`sys_fork()`:
1. 深拷贝页目录: 内核 PDE 共享 (引用同一物理页表), 用户 PDE 全量复制 (新页表 + 新物理页 + memcpy 内容)
2. 子进程内核栈全新构建:
   - 复制父进程 iret 帧 (相同 SS/ESP/EFLAGS/CS/EIP)
   - 复制父进程 pushal 帧, 但 **EAX 改为 0** (子进程 fork 返回值)
   - 压入 `fork_trampoline` 作为 switch_to 的返回地址
3. 子进程入就绪队列, 父进程返回子 PID

### 子进程栈布局 (fork 恢复)

```
sp → [fork_trampoline]        ← switch_to ret 到这里
      [EDI][ESI][EBP][orig_ESP][EBX][EDX][ECX][EAX=0]
      [EIP][CS][EFLAGS][ESP_user][SS]
      ← fork_trampoline: 设用户DS → pop pushal → EAX=0 → iret
```

### exit(status) + wait(&status)

`sys_exit(status)`: 标记 ZOMBIE, 存 exit_status, 若父进程 BLOCKED 则唤醒, 调度下一线程

`sys_wait(&status)`: 遍历 PCB 找 ZOMBIE 子进程 → 收集 status, 释放资源, 返回子 PID。无 ZOMBIE 但有 RUNNING 子进程 → 设 BLOCKED, 切到其他线程 (被子进程 exit 唤醒)

### 调度器增强

每处 `switch_to` 之前调用 `switch_prepare(next)`:
```c
tss_set_esp0(next->stack + PROC_STACK_SIZE);  // Ring3→Ring0 时 CPU 从 TSS 读 ESP0
if (next->cr3 != current_cr3)
    vmm_switch_pd(next->cr3);                 // 切换地址空间
```

## 修复的关键 Bug

### Bug 1: 栈地址共享 (#UD Invalid Opcode)
`p->stack = stacks[p->pid - 1]` 在 `p->pid = next_pid++` **之前**执行, p->pid 未初始化(BSS=0), 所有线程用 `stacks[-1]` 拿到同一地址。

### Bug 2: proc_exit 上下文破坏
`switch_to(&next->ctx, &next->ctx)` — old 和 new 同指针, save 覆盖了 next 的上下文。改用内联汇编直接加载。

### Bug 3: kfree 静态栈
`proc_exit()` 对静态 BSS 数组调用 `kfree()` 导致堆损坏。改用 `kstack_alloc/kstack_free` 统一管理。

### Bug 4: 退格无效
`putchar('\b')` 在 VGA 文本模式只显示字符 0x08 的图块。改为 `putbackspace()` 直接操作光标。

### Bug 5: fork 子进程栈错误
`sys_fork` 用 memcpy 复制父进程内核栈 → `child->ctx.esp` 指向 `sys_fork` 内部 → `switch_to` 的 `ret` 跳到随机地址。改为全新构建子进程栈 (iret帧 + pushal + fork_trampoline)。

### Bug 6: vmm_switch_pd 缺失
直接 `mov cr3` 不更新 `current_pd_phys`, 导致后续 `vmm_map_page` 写到错误的页目录。新增 `vmm_switch_pd()` 原子更新全局变量 + CR3。

## 实机验证输出

```
Oh-my-os M4 Shell
> user

=== Creating Ring3 user process ===
[PROC] User proc PID=1 cr3=0x166000 entry=0x400000 stack=0xB0000000
[FORK] Parent PID=1 → Child PID=2 cr3=0x16B000
[WAIT] PID=1 blocking (no exited children)
[PROC] PID=2 exit(42)
[PROC] Woke parent PID=1
[WAIT] PID=1 collected child PID=2 status=42
[PROC] PID=1 exit(0)

User process test complete.
```
(ASUS X542UF, i5-8250U, 实机跑通)

## 文件清单

| 文件 | 功能 |
|------|------|
| `kernel/proc/proc.h` | PCB + cpu_context 定义 |
| `kernel/proc/proc.cc` | 调度器 + proc_create_user + sys_fork/exit/wait |
| `kernel/arch/x86/switch.S` | switch_to + ring3_trampoline + fork_trampoline |
| `kernel/arch/x86/isr_stubs.S` | isr_syscall 独立入口 |
| `kernel/arch/x86/gdt.h` | GDT/TSS 结构定义 + 选择子常量 |
| `kernel/arch/x86/gdt.cc` | GDT 初始化 (6段) + TSS 初始化 |
| `kernel/syscall/syscall.h` | syscall 号常量 |
| `kernel/syscall/syscall.cc` | syscall_handler 分发 |
| `kernel/kernel.cc` | 内核主函数 + Shell |
| `kernel/lib/printk.cc` | 控制台输出 + 滚动回溯 + 退格 |
| `kernel/drivers/keyboard/keyboard.cc` | PS/2 键盘驱动 |
| `kernel/mm/vmm.cc` | vmm_switch_pd 页目录切换 |
| `user/init/init_prog.S` | 用户态测试程序 (getpid→fork→exit→wait) |
| `Makefile` | 新文件编译 + 用户程序嵌入 |

## 已验证

- ✅ 内核线程创建/销毁 (QEMU + 实机)
- ✅ 协作式 yield (线程主动让出)
- ✅ PIT 抢占调度 (100Hz)
- ✅ 上下文保存/恢复
- ✅ 交互式 Shell (退格/回显/命令解析)
- ✅ 滚动回溯 (上下键/PgUp/PgDn)
- ✅ Ring3 用户进程创建 (GDT+TSS+CR3)
- ✅ fork 页目录深拷贝 + 子进程恢复
- ✅ exit(status) + 父进程唤醒
- ✅ wait(&status) + 僵尸收集
- ✅ 完整链路 fork→child exit→parent wait→parent exit (实机)

## 后续 (M5+)

- exec() 加载程序 (需要文件系统)
- 写时复制 (COW) fork 优化
- 信号量/互斥锁
- 多用户进程并发
- kmalloc 栈 (替代静态 BSS)

---

*2026-05-31, Commits: `1bc51cb` (Part 1), `c555a20` (Part 2)*
