# M2 阶段文档: 中断系统

> 日期: 2026-05-31
> 状态: ✅ 完成
> 实机验证: ASUS X542UF 通过 (PIT tick + 键盘回显)

## 一、阶段目标

实现 x86 中断/异常处理框架：CPU 能响应 32 种异常、16 个硬件 IRQ、以及 `int 0x80` 系统调用。时钟每秒 tick 100 次，键盘按键实时回显。

## 二、实机输出

```
+------------------------------------------+
|  Oh-my-os Kernel v0.0.2 (M2)             |
+------------------------------------------+

[INIT] Setting up IDT...
[INIT] IDT: 256 gates installed
[INIT] Initializing PIC...
[INIT] PIC: IRQ remapped to 0x20-0x2F
[PIT] Initialized: 100 Hz (divisor=11931, counter=11674)
[KBD] PS/2 keyboard initialized (IRQ1)
[INIT] EFLAGS before STI: 0x6
[INIT] PIC IMR: 0xFC / 0xFF
[INIT] PIC IRR: 0x0 / 0x0
[INIT] Enabling STI...
[TEST] Triggering software IRQ0 (int $0x20)...
[TEST] After int $0x20: jiffies=2 (expect 1)
[TEST] Triggering software IRQ1 (int $0x21)...
[TEST] After int $0x21: keyboard IRQ triggered

M2: waiting for hardware interrupts...

[TICK] jiffies=100
[TICK] jiffies=200
```

## 三、架构

```
                 ┌─────────────┐
                 │    IDT      │ 256 个中断门
                 │ (2KB)       │ 加载到 IDTR
                 └──┬──┬──┬───┘
        ┌───────────┘  │  │  └───────────┐
        ▼              ▼  ▼              ▼
    ┌──────┐   ┌──────────┐   ┌──────────────┐
    │ #DE  │   │  IRQ 0-15 │   │  int 0x80    │
    │ #PF  │   │  (0x20-   │   │  (系统调用)   │
    │ #GP  │   │   0x2F)   │   │  DPL=3       │
    │ ...  │   └────┬─────┘   └──────────────┘
    └──┬───┘        │
       ▼            ▼
  isr_common   irq_common
  (pushal/     (pushal/
   pushw/       pushw/
   leal 40/     leal 40/
   call         call
   isr_handler) irq_handler)
       │            │
       ▼            ▼
  异常 dump     pic_send_eoi(irq)
   + halt       → irq_handlers[irq]()
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   pit_handler  kbd_handler  (未来)
   jiffies++    扫描码→ASCII  ...
```

## 四、文件清单

| 文件 | 行数 | 职责 |
|------|------|------|
| `kernel/arch/x86/port.h` | 55 | inb/outb/inw/outw/inl/outl + io_wait |
| `kernel/arch/x86/idt.cc` | 95 | 256 中断门设置 + LIDT |
| `kernel/arch/x86/idt.h` | 35 | 描述符/帧结构体 |
| `kernel/arch/x86/isr_stubs.S` | 160 | 48 个汇编桩 + 公共分发器 |
| `kernel/arch/x86/isr.cc` | 42 | CPU 异常 panic + IRQ 分发 |
| `kernel/arch/x86/isr.h` | 15 | irq_register() API |
| `kernel/arch/x86/pic.cc` | 56 | 8259A 重映射 + EOI + 屏蔽 |
| `kernel/arch/x86/pic.h` | 25 | PIC 端口/常量 |
| `kernel/arch/x86/pit.cc` | 48 | 8253 方波 100Hz |
| `kernel/arch/x86/pit.h` | 17 | jiffies 声明 |
| `kernel/drivers/keyboard/keyboard.cc` | 125 | PS/2 驱动 + 扫描码→ASCII |
| `kernel/drivers/keyboard/keyboard.h` | 13 | kbd_getchar/init |
| `kernel/kernel.cc` | 65 (M2 部分) | 顺序初始化 + 诊断 |

## 五、关键技术实现

### 5.1 PIC 重映射

8259A 默认 IRQ0-7 → 向量 0x08-0x0F, IRQ8-15 → 0x70-0x77。但 0x08-0x0F 已被 CPU 异常 #8-#15 占用。不重映射 = IRQ0 (时钟) 和 #DF (双重故障) 完全不可区分。

```
重映射前: IRQ0 → 0x08 (恰好 = #DF)
重映射后: IRQ0 → 0x20, IRQ1 → 0x21, ..., IRQ15 → 0x2F
```

ICW 序列 (4 次写入, 每次需 I/O 延迟):

```
ICW1: 0x11 (边沿触发 + 级联 + ICW4)
ICW2: 0x20 / 0x28 (向量偏移)
ICW3: 0x04 / 0x02 (级联: 主片 IRQ2→从片)
ICW4: 0x01 (8086 模式, 手动 EOI)
```

### 5.2 中断栈帧

每个中断桩压入 `int_no` 后跳到公共分发器。分发器压入全部寄存器，构造栈帧传给 C++ handler:

```
栈布局 (从上到下):
  [GS:FS:ES:DS]    ← pushw × 4, 共 8B
  [EDI ESI EBP ESP EBX EDX ECX EAX]  ← pushal, 32B
  ──────────────── leal 40(%esp) 指向这里
  [int_no]         ← 桩压入
  [err_code]       ← 桩压入 (或 CPU 自动压入)
  [EIP CS EFLAGS]  ← CPU 自动压入
```

**关键 Bug:** 最初 `pushl %esp` 传的是压完寄存器后的栈顶地址, C++ 中读到 `frame->int_no` 实际是 EAX 值。`irq_handler` 中 `irq = 错误值 - 32` → 越界调用 → jiffies 永远不动。修复: `leal 40(%esp), %eax; pushl %eax`。

### 5.3 PIT 8253 配置

方波发生器模式 (Mode 3): 每半个周期翻转一次输出, 产生对称方波。

```
频率 = 1193180 / divisor
divisor = 11931 → 100.006 Hz (误差 0.006%)
```

PIT 通道 0 → IRQ0 → PIC → CPU INT 0x20 → irq0 stub → pit_handler → jiffies++。

实机需要充分的 I/O 延迟, 仅靠 `outb(0x80)` 不够。本实现用 `volatile` 计数循环 + `pause` 指令确保 PIT 正确锁存数据。

### 5.4 PS/2 键盘

AT Set 2 协议: 每个按键产生 1-3 字节序列。核心是状态机:

```
收到 0xF0 → expect_break = true → 下一个字节是 break code
收到 0xE0 → 忽略 (扩展码前缀, 方向键等)
收到 0x12/0x59 → Shift (Make) → shift_pressed = true
收到 0xF0 + 0x12/0x59 → Shift (Break) → shift_pressed = false
普通 make code → 查表转 ASCII → 入队 + 回显
```

扫描码→ASCII 映射表覆盖标准 US QWERTY 键盘 (59 个键)。Shift/Caps Lock 切换大小写和符号。

### 5.5 系统调用门

`int 0x80` 已注册为 DPL=3 陷阱门。用户态代码 (Ring 3) 可通过此门进入内核。当前仅注册了门, 处理函数待 M7 (用户态) 实现。

## 六、中断向量分配

```
0x00 - 0x1F   CPU 异常 (32 个, 部分保留)
  0x00  #DE  除零
  0x02  NMI  不可屏蔽中断
  0x06  #UD  无效操作码
  0x08  #DF  双重故障
  0x0D  #GP  通用保护
  0x0E  #PF  缺页 ← M3 内存管理核心

0x20 - 0x27   IRQ 0-7 (主 PIC)
  0x20  IRQ0  PIT 时钟
  0x21  IRQ1  PS/2 键盘

0x28 - 0x2F   IRQ 8-15 (从 PIC)
  0x28  IRQ8  RTC 实时时钟

0x30 - 0x7F   保留 (未来使用)

0x80          int 0x80 系统调用 (兼容 Linux ABI)
```

## 七、实机踩坑

| 现象 | 根因 | 修复 |
|------|------|------|
| jiffies=0, 无 tick | ISR 栈帧指针偏移 40B 错误 | `leal 40(%esp), %eax` |
| PIT 无输出, counter=0 | PIT I/O 延迟不够 | volatile 计数循环替代 outb(0x80) |
| Shift "粘住" | break code 未处理 | 状态机处理 0xF0/0xE0 多字节序列 |
| 屏幕字符显示异常 | 清屏用 0x0F00 (NULL) 非空格 | `clear_screen()` 用 0x0F20 |

## 八、下一步 (M3)

内存管理: 物理页框分配器 + 二级页表 + kmalloc/kfree + 按需分页 (#PF 处理)。
