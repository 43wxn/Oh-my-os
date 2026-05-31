# Oh-my-os M4 阶段报告：进程管理

## 阶段目标

实现内核级多线程调度：PCB、上下文切换、协作式让出、PIT 抢占调度、交互式 Shell。

## 架构概览

```
┌──────────────────────────────────────────────────────┐
│                    Shell (kernel.cc)                  │
│  > help / clear / info / echo / m4test / crash       │
├──────────────────────────────────────────────────────┤
│  调度器 (kernel/proc/proc.cc)                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │  PCB A   │  │  PCB B   │  │  PCB C   │  ... (16) │
│  │  esp:... │  │  esp:... │  │  esp:... │           │
│  │  ebp:... │  │  ebp:... │  │  ebp:... │           │
│  │  stack   │  │  stack   │  │  stack   │           │
│  └──────────┘  └──────────┘  └──────────┘           │
│       环形就绪队列 (Round-Robin)                       │
├──────────────────────────────────────────────────────┤
│  上下文切换 (kernel/arch/x86/switch.S)                 │
│  switch_to(old, new): 保存/恢复 ESP/EBP/EBX/ESI/EDI  │
├──────────────────────────────────────────────────────┤
│  PIT 100Hz → proc_tick() → proc_schedule_check()     │
│  (抢占式调度: IRQ 返回前检查是否需要切换)              │
└──────────────────────────────────────────────────────┘
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
    int          pid;      // 进程 ID
    int          state;    // PROC_ZOMBIE / READY / RUNNING
    cpu_context  ctx;      // 寄存器快照
    void        *stack;    // 内核栈底
    uint32_t     cr3;      // 页目录物理地址
    pcb         *next;     // 就绪队列链表
};
```

`PROC_MAX=16`, `PROC_STACK_SIZE=4096`。

## 调度器实现

| 函数 | 功能 |
|------|------|
| `proc_init()` | 初始化 PCB 表 (全部标记 ZOMBIE) |
| `proc_create(entry)` | 分配 PCB + 栈, 预设上下文, 加入就绪队列 |
| `proc_yield()` | 当前线程让出 CPU, 切换到就绪队列头部 |
| `proc_exit()` | 标记 ZOMBIE, 直接跳转到下一个线程 |
| `proc_tick()` | PIT 回调, 设置 `need_resched=1` |
| `proc_schedule_check()` | IRQ 返回前检查, 必要时强制切换 |

### 调度流程

1. **协作式** (`proc_yield`): 线程主动调用 → cli → 取队头 → 当前回队尾 → switch_to → sti
2. **抢占式** (PIT 100Hz): IRQ0 → proc_tick → 置 need_resched → irq_common → proc_schedule_check → switch_to → iret 到新线程

### 新线程栈布局

```
  sp → [edi=0] [esi=0] [ebx=0] [ebp=0]
        [ret=entry_fn]        ← switch_to 的 ret 跳到这里
        [ret2=proc_exit_hook] ← entry 返回后自动调用 proc_exit()
```

## 修复的关键 Bug

### Bug 1: 栈地址共享 (#UD Invalid Opcode)
`p->stack = stacks[p->pid - 1]` 在 `p->pid = next_pid++` **之前**执行, p->pid 未初始化(BSS=0), 所有线程用 `stacks[-1]` 拿到同一地址, 互相踩栈。

### Bug 2: proc_exit 上下文破坏
`switch_to(&next->ctx, &next->ctx)` — old 和 new 是同一指针, save 步骤用当前寄存器的值**覆盖**了 next 的保存上下文。改用内联汇编 + 寄存器约束直接加载。

### Bug 3: kfree 静态栈
`proc_exit()` 对静态 BSS 数组调用 `kfree()`, 导致堆损坏。已跳过(加 TODO 标记后续用 kmalloc 栈时恢复)。

### Bug 4: 退格无效
`putchar('\b')` 在 VGA 文本模式下只显示字符 0x08 的图块(白框), 不移动光标。改为直接操作 `cursor_col` + 写空格擦除的 `putbackspace()` 函数。

## 交互式 Shell

```
Oh-my-os M4 Shell
=================
Init OK.  paging=1  pit=100Hz  ticks=0

Oh-my-os Shell  (type 'help')
> help
Commands:
  help   - show this
  clear  - clear screen
  info   - system info
  echo X - print X
  m4test - M4 scheduler test
  crash  - trigger #PF (test exception)

> m4test
=== M4 Scheduler Test ===
Creating 3 threads A/B/C, each yields 5 times...

[A:0] [B:0] [C:0] [A:1] [B:1] [C:1] ...
[A EXIT] [B EXIT] [C EXIT]
M4 test done.
```

## 文件清单

| 文件 | 功能 |
|------|------|
| `kernel/proc/proc.h` | PCB + cpu_context 定义 |
| `kernel/proc/proc.cc` | 调度器完整实现 |
| `kernel/arch/x86/switch.S` | 上下文切换汇编 |
| `kernel/arch/x86/isr_stubs.S` | IRQ 公共入口 (调用 proc_schedule_check) |
| `kernel/kernel.cc` | 内核主函数 + Shell |
| `kernel/lib/printk.cc` | 控制台输出 + 滚动回溯 + 退格 |
| `kernel/drivers/keyboard/keyboard.cc` | PS/2 键盘驱动 |

## 已验证 (QEMU)

- ✅ 线程创建/销毁
- ✅ 协作式 yield (线程主动让出)
- ✅ PIT 抢占调度 (100Hz 时钟强制切换)
- ✅ 上下文保存/恢复
- ✅ 线程退出 + 自动回收 PCB
- ✅ 交互式 Shell (退格/回显/命令解析)
- ✅ 滚动回溯 (上下键/PgUp/PgDn)

## 待验证 (实机)

- 线程调度在 ASUS X542UF 上的运行 (上次栈 bug 导致 #UD)

## 后续 (M4+)

- fork/exec/exit/wait 系统调用
- 用户态 (Ring3) 进程
- 信号量/互斥锁
- kmalloc 栈 (替代静态 BSS)

---

*2026-05-31, Commit: `1bc51cb`*
