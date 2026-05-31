# HACKING — 如何从零开发一个能真正使用的 OS

## 原则

1. **每一行代码都要在实机上验证。** QEMU 是快捷方式，不是最终目标。QEMU 跑通了不代表实机能跑——中断控制器、AHCI 时序、VGA BIOS 的行为在模拟器和实机上会有差异。
2. **分层递进，下层不稳上层不写。** 中断没调好就不写进程调度，页表没映射对就不碰文件系统。OS 的每一层都依赖下一层的正确性，bug 越底层越难排查。
3. **先跑起来再优化。** 第一个版本允许丑、允许硬编码、允许低效。能跑是最难的事，优化是第二难的事。
4. **代码在开发机上写，产物烧到 U 盘，笔记本验证。** 开发机 = 台式机（写代码 + 编译），目标机 = ASUS X542UF 笔记本（实机运行）。

## 目标：一个真正能用的命令行操作系统

去掉图形界面之后，一个"真正能用"的 OS 至少具备：

| 能力 | Linux/BSD 有 | 我们做不做 | 优先级 |
|------|:-----------:|:--------:|:------:|
| **进程管理**（fork/exec/kill/调度） | ✓ | ✓ | M4 |
| **内存管理**（虚拟内存/按需分页） | ✓ | ✓ | M3 |
| **文件系统**（持久存储/目录/读写） | ✓ | ✓ | M5-M6 |
| **Shell**（命令解析/管道/重定向） | ✓ | ✓ | M7 |
| **系统调用** | ✓ | ✓ | M2 |
| **驱动**（磁盘/键盘/显示） | ✓ | ✓ | M1/M2/M5 |
| **进程间通信**（管道/信号） | ✓ | ✓ | M8 |
| **网络**（TCP/IP + 以太网驱动） | ✓ | ✓ | M9 |
| **电源管理**（关机/重启） | ✓ | ✓ | M10 |
| **多核**（SMP，用满 8 个线程） | ✓ | ✓ | M11 |
| ext4/NTFS 驱动 | ✓ | ✗ | 非必需，FAT32 够用 |
| USB 外设（键鼠之外的设备） | ✓ | ✗ | 依赖 XHCI 栈，后期 |
| 图形界面 | ✓ | ✗ | 明确不要 |
| 音频 | ✓ | ✗ | 非必需 |
| 多用户/权限管理 | ✓ | ✗ | 个人 OS，无需 |
| Wi-Fi 驱动 | ✓ | ✗ | Atheros AR9565 驱动极其复杂 |

**目标定位：** 一个单用户命令行 OS，可以：
- 开机启动进入 shell，执行命令
- 在 FAT32 磁盘上持久存储文件
- 通过以太网上网（有线网卡 RTL8111）
- 用满 CPU 的 4 核 8 线程跑并发任务
- 按电源键或敲 `shutdown` 正常关机
- 通过 shell 管道组合命令
- **运行第三方开源软件**（vim、gcc、curl）——这是脱离玩具属性的唯一标志

---

## 核心设计原则：如何不写成玩具

以下是三条贯穿全部 11 个阶段的硬性设计约束。每写一个模块都要对照这三条。

### 原则 A：POSIX 兼容的系统调用接口

**不做的事：** 随意定义自己的 `int 0x80` 参数约定（比如 ax=1 是 my_print, ax=2 是 my_fork...）。

**必须做的事：** 系统调用的编号、参数、返回值语义**完全兼容 Linux i386 ABI**。

```
系统调用接口 = Linux 32-bit ABI
  ├── 触发方式: int 0x80
  ├── 编号寄存器: eax
  ├── 参数寄存器: ebx, ecx, edx, esi, edi, ebp
  ├── 返回值: eax (负数 = -errno)
  └── 编号集: Linux 系统调用号的子集
```

**关键 Linux 系统调用号 (i386)：**

| 编号 | 名称 | 说明 | 阶段 |
|------|------|------|------|
| 1 | exit | 进程退出 | M4 |
| 3 | read | 读文件描述符 | M6 |
| 4 | write | 写文件描述符 | M2 |
| 5 | open | 打开文件 | M6 |
| 6 | close | 关闭文件描述符 | M6 |
| 11 | execve | 执行程序 | M7 |
| 20 | getpid | 获取 PID | M4 |
| 37 | kill | 发送信号 | M8 |
| 41 | dup | 复制 fd | M8 |
| 42 | pipe | 创建管道 | M8 |
| 45 | brk | 扩展堆 | M3 |
| 54 | ioctl | 设备控制 | M6 |
| 63 | dup2 | 复制 fd 到指定号 | M8 |
| 90 | mmap (old) | 内存映射 | M3 |
| 91 | munmap | 解除映射 | M3 |
| 102 | socketcall | socket 操作 | M9 |
| 120 | clone | 创建进程 (fork 底层) | M4 |
| 252 | exit_group | 线程组退出 | M11 |

未实现的系统调用返回 `-ENOSYS`（-38）。musl libc 遇到 `ENOSYS` 会自动 fallback 或报错。

**为什么是 Linux ABI 而不是自己定义？**
因为 musl libc 已经实现了"POSIX C 函数 → Linux syscall"的映射层。复用 Linux ABI = 自动获得 musl 支持 = 可以跑调用 musl 编译的任何程序（包括 vim、gcc、curl、python）。

### 原则 B：先自写简化 libc 验证，最终移植 musl

**两个阶段，各有目的：**

| | 自写简化 libc | 移植 musl |
|---|---|---|
| **时机** | M2-M6 | M7 完成后 |
| **目的** | 验证系统调用链路正确 | 获得 POSIX C 标准库的全部能力 |
| **规模** | ~500 行（printf/malloc/string） | ~80,000 行（完整 C11 + POSIX） |
| **关键价值** | 快速迭代，不依赖外部代码 | 能跑 vim/gcc/curl/python |

**musl 移植要改什么：**

```
musl 需要的最小内核接口:
  ├── 约 50 个 Linux 系统调用 (read/write/open/close/fork/execve/mmap/...)
  ├── /proc 伪文件系统 (可选，但 musl 某些函数需要)
  ├── 合理的进程地址空间布局 (代码段/数据段/堆/栈/mmap 区)
  └── 动态链接器 ld-musl (早期可先用静态链接，省掉这一步)
```

**移植策略：**
1. M7 阶段：用自写 libc 把 shell 跑起来，证明用户态链路通
2. M8-M9 阶段：逐步补充 musl 需要的系统调用
3. M10 前后：用 `i686-linux-musl-g++` 交叉编译一个简单的 C++ 程序（如 busybox），放系统上运行
4. 修复 `ENOSYS` 缺口，直到没有 stub 报错
5. 终极验证：交叉编译 vim，在 Oh-my-os 上打开并编辑一个文件

### 原则 C：一切皆文件的 VFS 抽象

**不做的事：** 文件系统只管理 FAT32 磁盘上的文件，键盘/屏幕/管道/网络各走各的特殊接口。

**必须做的事：** 一切 I/O 资源都通过文件描述符和 VFS 操作表访问。

```
VFS 层 (统一接口)
  │
  ├── 磁盘文件 (FAT32)
  │     └── ops: read=disk_read, write=disk_write
  │
  ├── 设备文件 (/dev/)
  │   ├── /dev/tty     → 键盘输入 + 屏幕输出
  │   │   └── ops: read=kbd_read, write=vga_write
  │   ├── /dev/sda     → 磁盘块设备
  │   │   └── ops: read=block_read, write=block_write
  │   └── /dev/null    → 黑洞
  │       └── ops: read=return_0, write=discard
  │
  ├── 管道 (pipe)
  │     └── ops: read=pipe_read, write=pipe_write (环形缓冲区)
  │
  └── Socket (网络)
        └── ops: read=tcp_read, write=tcp_write, ...
```

**每个打开的文件是个 `struct file`：**

```c
struct file {
    int    fd;             // 文件描述符编号
    int    flags;          // O_RDONLY / O_WRONLY / O_RDWR
    off_t  pos;            // 当前读写位置
    void  *private;        // 私有数据 (inode*, pipe_buf*, socket*...)
    struct file_ops *ops;  // 操作表
};

struct file_ops {
    int  (*read)   (struct file *f, void *buf, size_t n);
    int  (*write)  (struct file *f, const void *buf, size_t n);
    int  (*ioctl)  (struct file *f, int cmd, void *arg);
    int  (*close)  (struct file *f);
    int  (*seek)   (struct file *f, off_t offset, int whence);
};
```

**为什么这很重要：**

因为 shell 的 `ls | grep foo > /tmp/out` 依赖：
- `pipe()` 创建管道 → 返回两个 fd
- `dup2()` 把 stdout 重定向到管道写端
- `grep foo` 的 stdin 从管道读端读
- `>` 把 stdout 重定向到文件的 fd

如果每种 I/O 走不同接口，shell 就得为 N 种资源写 N 套代码。VFS 统一 fd 抽象后，shell 根本不知道也不关心 `fd 0` 到底是键盘还是文件还是 pipe——它只调 `read(fd, buf, n)`。

---

## 开发路线图

```
  M0 (✅)              M1                  M2                  M3
  ┌────────┐       ┌────────┐          ┌────────┐          ┌────────┐
  │Demo OK │  ──▶  │Bootloader│  ──▶   │中断系统 │  ──▶   │内存管理 │
  │512B MBR│       │+保护模式 │         │PIC+PIT │          │页表+堆  │
  └────────┘       └────────┘          │+键盘输入│          └────────┘
                        │              └────────┘               │
                        ▼                   ▼                   ▼
                   实机显示            键盘中断响应          kmalloc 成功
                   "Stage2 OK"        "Key: A" 输出       页表映射正常
                    
  M4                  M5                  M6                  M7
  ┌────────┐       ┌────────┐          ┌────────┐          ┌────────┐
  │进程管理 │  ──▶  │AHCI驱动 │  ──▶   │文件系统 │  ──▶   │用户态   │
  │调度+同步│       │磁盘读写 │         │FAT32   │         │ELF+Shell│
  └────────┘       └────────┘          └────────┘          └────────┘
       │                │                   │                   │
       ▼                ▼                   ▼                   ▼
  进程切换正常       ATA IDENTIFY      ls/cat 命令可用     ohm$ 提示符
  ps 命令可用        扇区读写成功       文件持久存储        exec 用户程序

  M8                  M9                  M10                 M11
  ┌────────┐       ┌────────┐          ┌────────┐          ┌────────┐
  │ IPC+管道│  ──▶  │网络栈  │  ──▶    │ ACPI   │  ──▶    │ SMP    │
  │信号+pipe│       │TCP/IP  │         │电源管理│          │多核调度│
  └────────┘       └────────┘          └────────┘          └────────┘
       │                │                   │                   │
       ▼                ▼                   ▼                   ▼
  echo \| wc 工作    ping/curl 成功     shutdown 关机      8 核并发运行
  kill -9 终止       从网络下载文件      按电源键响应       并行编译内核
```

每个里程碑的产出是一段**可以在实机上运行并观察结果的代码**，不是文档或设计图。

---

## 模块依赖关系

```
boot/                       ← 不依赖任何东西，纯汇编
  │
  └──▶ kernel/arch/x86/     ← 依赖: 无。GDT/IDT/PIC/PIT/port I/O
           │
           ├──▶ kernel/mm/         ← 依赖: 物理内存探测 (E820 from boot)
           │
           ├──▶ kernel/drivers/    ← 依赖: PCI枚举, MMIO, IRQ
           │      ├── vga/         ← 依赖: port I/O
           │      ├── keyboard/    ← 依赖: IRQ1, PIC
           │      └── ahci/        ← 依赖: PCI, IRQ, MMU
           │
           ├──▶ kernel/proc/       ← 依赖: MMU (页表切换), PIT (时钟)
           │
           ├──▶ kernel/fs/         ← 依赖: AHCI (块设备), MM (缓冲区)
           │
           └──▶ kernel/lib/        ← 依赖: 无。printk/string

user/                       ← 依赖: kernel (系统调用接口)
  ├── libc/                 ← 依赖: int 0x80 包装
  ├── init/                 ← 依赖: libc + kernel
  ├── shell/                ← 依赖: libc + VFS
  └── programs/             ← 依赖: libc
```

**严格自底向上。** 下层模块对外暴露的接口在头文件里定义，上层只通过头文件调用。

---

## 每个阶段的开发循环

```
1. 写代码 (C + 少量汇编)
       │
2. make → 编译 + 链接
       │
3. make qemu → QEMU 秒级验证 (快速迭代)
       │  ├── 通过了 ──▶ 4
       │  └── 崩了 ──▶ GDB 调试 → 修 bug → 回到 1
       │
4. make image → 生成磁盘镜像
       │
5. dd → U 盘 → 笔记本实机验证
       │  ├── 通过了 ──▶ 提交 git, 进入下一个子模块
       │  └── 崩了 ──▶ 对比 QEMU 和实机的差异 → 修 bug → 回到 1
```

**不要攒一堆代码再验证。** 每写一个小功能（比如"IDT 初始化"）就跑一次 QEMU。QEMU 启动只要 2 秒，试错成本极低。实机验证可以积累几个功能点再做，减少插拔 U 盘的次数。

---

## M0 → M1: 从 Demo 到真正的 Bootloader

**目标:** 512B MBR → 加载 Stage2 → 实模式切换到 32 位保护模式 → 加载 kernel.elf → 跳转。

**关键要解决的问题:**

| 问题 | 解决方案 |
|------|----------|
| MBR 只有 512B，放不下保护模式切换和 ELF 加载 | MBR 只加载 Stage2，Stage2 做重活 |
| 怎么从磁盘读扇区 | BIOS INT 0x13 (AH=0x42, LBA 读) |
| 怎么知道 kernel 在磁盘的哪个位置 | 固定扇区号，或读 FAT32 目录 |
| A20 地址线 | 键盘控制器 (0x64) 或 BIOS INT 0x15 |
| 切换保护模式后 BIOS 中断不能用了 | 所有屏幕输出必须在切换前完成或用 VGA 显存 |
| ELF 解析 | 读 Program Header Table，把每个 `PT_LOAD` segment 复制到指定地址 |

**产物:**
```
boot/
├── mbr.asm        ← 512B, 加载 stage2 到 0x1000
├── stage2.asm     ← A20 + GDT + 保护模式 + ELF 加载 + 跳转
└── a20.asm        ← A20 门开启（独立单元，方便调试）
```

**验证标准:** 实机上 VGA 显示 "Entered protected mode. Jumping to kernel..."

---

## M1 → M2: 中断系统

**目标:** CPU 能响应硬件中断和软件异常，时钟滴答产生，键盘按键有反应。

**实现顺序:**

```
1. IDT (中断描述符表)
   └── 256 个门描述符，前 32 个 = CPU 异常，后 16 个 = IRQ
   
2. 8259A PIC 初始化 & 重映射
   └── 把 IRQ0-15 映射到中断向量 0x20-0x2F
       不重映射的话 IRQ0 会与 CPU 异常 #8 (Double Fault) 冲突
   
3. PIT 8253 时钟中断
   └── IRQ0 → 100Hz tick → jiffies++
       先只计数，后续给调度器用
   
4. PS/2 键盘中断
   └── IRQ1 → 读端口 0x60 → 扫描码 → 环形缓冲区
   └── 先只处理按下（忽略释放），做扫描码→ASCII 映射
   
5. 系统调用框架 (int 0x80)
   └── 用中断向量 0x80 做 syscall 入口
   └── 先只实现 sys_write（VGA 输出）
```

**⚠️ 实机注意事项:**
- 8259A PIC 在实机上的初始化时序比 QEMU 严格——必须等每个初始化控制字写入后的延迟
- PS/2 键盘在实机上可能返回 `0xFA`（ACK），QEMU 不一定返回
- PIT 的频率计算要精确——`1193180 / frequency`，余数会影响精度

**验证标准:** 实机上每秒在屏幕上打印 `tick: N`，按键盘上的键能显示对应字符。

---

## M2 → M3: 内存管理

**目标:** 知道机器有多少内存、哪些区域可用；能分配和释放物理页；能创建页表映射虚拟地址。

**实现顺序:**

```
1. 物理内存探测
   └── Bootloader 阶段通过 BIOS E820 获取内存 map
   └── 传递到内核，解析出可用区域 (>1MB)
   
2. 物理页框分配器
   └── 位图 or 伙伴算法 or 空闲链表
   └── 接口: alloc_page(), free_page()
   
3. 二级页表 (10-10-12)
   └── PDE (Page Directory Entry) → PTE (Page Table Entry) → 物理页
   └── 接口: map_page(vaddr, paddr, flags), unmap_page(vaddr)
   
4. 内核堆 (kmalloc/kfree)
   └── 在页框分配器之上实现
   └── 小对象用 slab/slub 或简单链表
   
5. Demand Paging
   └── 缺页异常 (INT 0x0E) → 分配物理页 → 映射 → 返回
```

**⚠️ 实机注意事项:**
- E820 在某些 BIOS 上返回不可靠数据——需要验证每段是否可读写
- 0xB8000 (VGA 显存) 和 0x100000 (内核位置) 必须在页表中正确映射
- CR3 切换后 TLB 失效——注意性能，flush TLB 用 `invlpg` 或重新加载 CR3

**验证标准:** 实机上 `printk("Free memory: %d pages", free_pages)` 输出正确的可用页数。

---

## M3 → M4: 进程管理

**目标:** 多个进程并发运行，按时间片轮转调度，通过信号量/互斥锁同步。

**实现顺序:**

```
1. PCB 数据结构
   └── 进程 ID、状态、CPU 上下文 (寄存器快照)、页表 CR3、文件描述符表
   
2. 上下文切换
   └── 保存当前进程寄存器 → 切换 CR3 → 恢复目标进程寄存器
   └── 用汇编写 switch_to(prev, next)
   
3. 调度器
   └── 先 FCFS（简单验证），再 Round Robin（PIT 中断驱动）
   └── 调度入口: schedule() → pick_next() → switch_to()
   
4. 系统调用: fork / exec / exit / wait
   └── fork: 复制页表、PCB、文件描述符
   └── exec: 替换地址空间（加载 ELF → 新页表）
   └── exit: 释放资源 → 状态变 ZOMBIE
   └── wait: 等子进程退出 → 回收
   
5. 同步原语
   └── 信号量 (sem_wait/sem_post)、互斥锁 (mutex_lock/mutex_unlock)
```

**⚠️ 实机注意事项:**
- 进程切换是 bug 高发区——寄存器少保存一个，进程回来后行为异常但不会 crash
- TSS (Task State Segment) 必须配置——SS0 和 ESP0 用于从 Ring3→Ring0 时的栈切换
- 实机的 PIT 中断频率可能因主板晶振误差而偏离——不要假设精确 100Hz

**验证标准:** 实机上 `ps` 命令列出所有进程，`kill PID` 能终止进程，两个进程轮流打印各自字符不混乱。

---

## M4 → M5: AHCI 磁盘驱动

**目标:** 通过 SATA AHCI 控制器读写笔记本的 111.8GB 固态硬盘。

这是整个项目技术难度最高的模块。详见 [docs/ahci.md](docs/ahci.md)。

**实现顺序:**

```
1. PCI 枚举
   └── 扫描 PCI 配置空间，Class Code 0x010601 → AHCI 控制器
   └── 读 BAR[5] → ABAR MMIO 基址
   
2. HBA + Port 初始化
   └── HBA Reset → Port 探测 → Command List / FIS 内存分配
   
3. IDENTIFY DEVICE
   └── 发 ATA IDENTIFY 命令，读取磁盘参数 (型号/序列号/LBA48/扇区总数)
   
4. READ DMA EXT / WRITE DMA EXT
   └── LBA48 寻址，PRDT 描述数据缓冲区
```

**⚠️ 实机注意事项:**
- QEMU 的 AHCI 模拟相当完善，但实机上有细微时序差异
- Port Reset 后的等待循环在实机上可能需要加 `pause` 指令或 `io_wait`
- SSD 的 IDENTIFY 响应可能比 QEMU 快得多（微秒级），不要硬编码超时
- LBA48 读写后务必检查 `PxTFD.ERR`，SSD 可能有坏块或保护区域

**验证标准:** 实机上打印出 `SATA SSD: Vendor=..., Model=..., Size=111.8GB`。能读写任意扇区。

---

## M5 → M6: 文件系统

**目标:** 在 AHCI 磁盘之上建立 FAT32 文件系统，支持文件的创建/删除/读写。

**实现顺序:**

```
1. 块设备抽象层
   └── struct block_dev { read(lba, buf, count), write(lba, buf, count) }
   
2. FAT32 解析
   └── 读 VBR → 解析 BPB → 加载 FAT 表 → 根目录
   
3. VFS (虚拟文件系统)
   └── 统一接口: open/close/read/write/seek/readdir
   └── 每个进程的 fd_table
   
4. 文件操作
   └── 创建/删除/读写/目录遍历
   
5. RAMFS (开发用)
   └── 不依赖磁盘，纯内存文件系统
   └── 用于开发调试阶段（磁盘驱动没调好时也能测 fs 逻辑）
```

**验证标准:** 实机上 `ls /bin` 列出所有用户程序，`cat /etc/motd` 显示欢迎语，`echo "test" > /home/test.txt` 写入文件然后在 Deepin 下 mount 看得到。

---

## M6 → M7: 用户态 + Shell

**目标:** 用户程序运行在 Ring 3，通过系统调用获取内核服务。Shell 作为第一个真正的用户程序启动。

**实现顺序:**

```
1. Ring 3 切换
   └── 设置用户态段选择子 (RPL=3) → iret 到用户态
   
2. libc (简化版)
   └── syscall 包装: sys_write/call1/2/3 → int 0x80
   └── printf/malloc/free/atoi/exit
   
3. ELF 加载器
   └── 解析 ELF header → 分配页表 → 映射 segments → 初始化用户栈
   
4. init 进程 (PID 1)
   └── 挂载文件系统 → 启动 console → 启动 shell
   
5. Shell
   └── REPL: 读命令 → 解析 → 执行
   └── 内建命令: cd/exit/help
   └── 外部命令: 从 /bin/ 加载 ELF 执行
```

**验证标准:** 系统启动后出现 `ohm$ ` 提示符，可以输入 `ls`、`echo`、`cat`、`ps` 命令，结果正确。`exec /bin/test.elf` 运行独立程序。

---

## M7 → M8: IPC + 管道

**目标:** shell 支持管道（`|`）和重定向（`>` `<`），进程间能通过信号通信。

**实现顺序:**

```
1. 管道 (pipe)
   └── 内核创建环形缓冲区 (通常 4KB)
   └── 返回两个 fd: pipefd[0]=读端, pipefd[1]=写端
   └── 写端 close 后读端读到 EOF
   
2. 重定向
   └── dup2() 系统调用: 复制 fd 到指定编号
   └── shell 实现: cmd1 > file → open + dup2 + exec
   └── shell 实现: cmd1 | cmd2 → pipe + fork + dup2 + exec
   
3. 信号机制完善
   └── SIGKILL (不可捕获)、SIGTERM、SIGINT (Ctrl-C)
   └── sigaction / signal 系统调用
   └── 从键盘中断触发 SIGINT 发送到前台进程组
```

**验证标准:** `cat /etc/motd | wc -l` 输出文件行数，`ls > /tmp/list.txt` 创建文件再 `cat /tmp/list.txt` 内容正确。

---

## M8 → M9: 网络栈

**目标:** 通过有线以太网卡 (RTL8111) 获取 IP 地址，发送 TCP 请求。

**实现顺序:**

```
1. 以太网卡驱动
   └── Realtek RTL8111 (PCI: 02:00.0)
   └── MMIO + 寄存器: 发送/接收描述符环
   └── 支持 DMA 发送和中断接收
   
2. 以太网帧处理
   └── ARP 协议 (IP → MAC 地址解析)
   └── MAC 地址获取 (从 NIC EEPROM 读)
   
3. IPv4 协议
   └── IP 头构造/解析
   └── 分片与重组
   └── 校验和计算
   
4. ICMP 协议 (ping)
   └── 先实现 ICMP Echo Reply，验证网络通
   
5. UDP (可选，快速实现)
   └── UDP 套接字 → DNS 查询 → DHCP 获取 IP

6. TCP 协议
   └── 三次握手 (SYN → SYN-ACK → ACK)
   └── 滑动窗口 + 确认 + 重传 (简化版)
   └── 连接关闭 (FIN → FIN-ACK → ACK)
   
7. Socket API
   └── socket() / bind() / listen() / accept() / connect()
   └── send() / recv() / close()
   
8. 用户态工具
   └── ping — ICMP 探测
   └── dhclient — DHCP 获取 IP
   └── wget — HTTP GET (TCP 端口 80)
```

**⚠️ 实机注意事项:**
- RTL8111 的寄存器在 MMIO 空间，BAR 由 PCI 配置空间获取
- 发送/接收描述符环必须物理连续（或者用分页映射保证）
- DHCP 先不做自动配置可以手工设 IP: `ifconfig eth0 192.168.1.100/24`

**验证标准:** `ping 192.168.1.1` 收到 reply，`wget http://example.com` 下载 HTML 并输出到终端。

---

## M9 → M10: ACPI + 电源管理

**目标:** `shutdown` 命令能正常关机，`reboot` 能重启，按电源键有反应。

**实现顺序:**

```
1. ACPI 表解析
   └── RSDP → RSDT/XSDT → FADT/DSDT
   └── 启动时从 BIOS 内存区域搜索 "RSD PTR " 签名
   
2. 关机 (S5)
   └── ACPI: 写 SLP_TYPa=5, SLP_EN=1 到 PM1a_CNT 端口
   └── 备选: APM (老机型) 用 INT 0x15, AX=0x5307
   
3. 重启
   └── 8042 键盘控制器: 写 0xFE 到端口 0x64
   └── 备选: ACPI reset_reg
   └── 最暴力: `lidt null; int 0` (触发 triple fault)
   
4. 电源按钮事件
   └── ACPI Fixed Event → SCI 中断
   └── 处理: 发送 SIGTERM 给所有进程 → sync 磁盘 → 关机
```

**验证标准:** 敲 `shutdown` 笔记本断电，敲 `reboot` 笔记本重启，按电源键弹出 "shutting down..." 然后关机。

---

## M10 → M11: SMP 多核

**目标:** i5-8250U 的 4 核 8 线程全部参与调度，进程真正并行执行。

**实现顺序:**

```
1. ACPI MADT 解析
   └── 枚举 Local APIC 条目，获取每个 CPU 的 APIC ID
   
2. Local APIC 初始化
   └── MMIO 映射 LAPIC 基址 (通常 0xFEE00000)
   └── 校准 LAPIC Timer (用于每核时钟中断)
   
3. 启动 AP (Application Processor)
   └── BSP (Bootstrap Processor) 发送 INIT-SIPI-SIPI 序列
   └── AP 从实模式起步 → 切换到保护模式 → 进入 C++ 代码
   └── 每核独立内核栈 + 每核 PCB/TSS
   
4. 调度器 SMP 化
   └── 每核 runqueue (就绪队列)
   └── 负载均衡: work stealing 或全局队列 + 锁
   └── 每核 TLB 管理 (CR3 重载, TLB shootdown via IPI)
   
5. 内核锁
   └── 大内核锁 (Big Kernel Lock) — 起步方案
   └── 逐步细化为自旋锁 + RCU
```

**⚠️ 实机注意事项:**
- Local APIC 在实机上有 ID 映射差异 (xAPIC vs x2APIC)
- TLB shootdown 在实机上必须正确——用 IPI 通知其他核 `invlpg`
- 实机 BIOS 可能需要 setup ACPI tables 才能正确查到 APIC 信息
- Hyper-Threading: 8 个逻辑核中 (0,1) 共享物理核 0，(2,3) 共享物理核 1，等等。调度器需要考虑物理核亲和性

**验证标准:** `ps` 显示 8 个 CPU 核心，跑 8 个死循环进程每个 CPU 100%，`htop`-like 的 `/bin/top` 显示所有核心利用率。

---

## 最终：硬盘安装与双启动

**目标:** OS 不再依赖 U 盘启动，直接安装到笔记本硬盘，与 Deepin 共存。

详见 [docs/deploy.md](docs/deploy.md) 第六章。

**步骤:**
1. 从 sda7 分 8GB 给 Oh-my-os (sda8)
2. dd 系统镜像到 sda8
3. 修改 GRUB 配置，添加 Oh-my-os 启动项
4. 重启后在 GRUB 选择 Oh-my-os 进入

---

## 完成态：M11 之后你拥有什么

```
一个运行在华硕 X542UF 实机上的操作系统：

开机 → VGA 显示 boot log → 内核初始化 → init 启动 shell
  → ohm$ 提示符
  → 运行 /bin/ls, /bin/cat, /bin/ps, /bin/kill
  → shell 管道: cat /etc/motd | wc -l
  → 文件持久存在 FAT32 磁盘 (重启不丢失)
  → 以太网上网 (ping + wget 下载)
  → shutdown 正常关机 / reboot 重启
  → 4 核 8 线程满血运行

然后，换 musl libc，交叉编译第三方软件：
  → /bin/vim     — 在 Oh-my-os 上编辑 Oh-my-os 的源码
  → /bin/gcc     — 在 Oh-my-os 上自举编译一个 C 程序
  → /bin/curl    — 从互联网下载文件
```

| 能力维度 | M11 Oh-my-os | 典型 Linux | 差距说明 |
|----------|:-----------:|:----------:|----------|
| **POSIX 系统调用** | 约 50 个 (Linux ABI 子集) | ~400 个 | 够用，musl 只需约 50 个 |
| **libc** | musl (C11 + POSIX) | glibc/musl | 同等规格 |
| **VFS "一切皆文件"** | 磁盘/设备/管道/socket 统一 fd | ✓ | 同等设计 |
| **进程/内存/文件系统** | ✓ | ✓ | |
| **磁盘/键盘/网卡驱动** | ✓ | ✓ | |
| **管道与重定向** | ✓ (pipe + dup2 + VFS) | ✓ | 同等能力 |
| **TCP/IP + 以太网** | ✓ | ✓ | |
| **ACPI 关机/重启** | ✓ | ✓ | |
| **SMP 多核** | ✓ (8 线程) | ✓ | |
| **运行第三方软件** | ✓ (vim/gcc/curl) | ✓ | **最关键** |
| USB 外设 | ✗ | ✓ | 后期 |
| Wi-Fi | ✗ | ✓ | AR9565 驱动极其复杂 |
| 动态链接 | ✗ (仅静态链接) | ✓ | 后期 |
| ext4/XFS/btrfs | ✗ (仅 FAT32) | ✓ | FAT32 够用 |
| 多用户/权限 | ✗ | ✓ | 个人 OS 不需要 |

**M11 完成后的 Oh-my-os = 一个可以跑真实第三方开源软件的命令行 OS。** 你在上面用 vim 编辑文件，用 g++ 编译 C++ 程序，用 curl 下载——这三件事证明了它不是玩具。它是你自己从头写的。

---

## 编码规范

### 汇编

- 16 位实模式: `.code16`，Intel 语法 (`.intel_syntax noprefix`) 或 NASM
- 32 位保护模式: `.code32`，同上
- 文件名: `*.S` (需预处理的 GNU as) 或 `*.asm` (NASM)
- 必须写注释说明每段代码的目的

### C++

- 内核和用户态代码均使用 C++（`i686-elf-g++`）
- 文件扩展名：`.cc`（内核）和 `.h`（头文件）
- **禁止**：异常（`-fno-exceptions`）、RTTI（`-fno-rtti`）、STL、标准库（`-nostdlib`）
- **禁止**：浮点数（内核没有 FPU 上下文保存，SSE 寄存器不会被自动保存）
- **必须显式管理**：全局构造函数（`.init_array` 段需在内核启动时调用），纯虚函数需要提供 `__cxa_pure_virtual` 桩
- **与汇编接口**：所有被汇编调用的函数必须用 `extern "C"` 声明，避免 C++ 名字修饰（name mangling）导致链接失败
- **`new`/`delete`**：需在内核堆（`kmalloc`/`kfree`）之上实现 `operator new`/`operator delete`
- 头文件用 `#pragma once` 或 include guard: `#ifndef _KERNEL_MM_PMM_H ...`
- 内核代码放 `kernel/`，用户态代码放 `user/`
- 模块自包含：`foo.cc` + `foo.h` 在同一目录，`foo.h` 复制到 `include/kernel/`

**允许使用的 C++ 特性：**

| 特性 | 允许 | 说明 |
|------|:----:|------|
| 类/结构体 + 成员函数 | ✓ | 驱动/VFS/进程等天然适合 OOP |
| 模板 | ✓ | 数据结构（链表/哈希表等）用模板减少代码重复 |
| 命名空间 | ✓ | `kernel::mm`, `kernel::fs` 等避免符号冲突 |
| 引用 (`&`) | ✓ | |
| `constexpr` | ✓ | 编译期常量替代宏 |
| `static_cast`/`reinterpret_cast` | ✓ | |
| `enum class` | ✓ | 类型安全枚举 |
| `extern "C"` | ✓ | 汇编接口必须 |
| STL（vector/string/map...） | ✗ | 依赖异常 + 标准库 |
| `dynamic_cast` | ✗ | 依赖 RTTI |
| 异常（try/catch/throw） | ✗ | 运行时开销大，内核不应有异常 |
| 虚函数 | ⚠️ 谨慎 | 可以但需要手动提供 vtable 支持

### 提交信息

```
M2: 实现 IDT 和 PIC 初始化
- 256 个中断门描述符
- PIC 重映射 IRQ0-15 → 0x20-0x2F
- 测试: IRQ0 (时钟) 产生 ticks 计数
- QEMU 通过，实机待验证
```

---

## 调试技巧

### QEMU + GDB

```bash
# 启动 QEMU 并等待 GDB 连接
qemu-system-i386 -kernel kernel.elf -s -S

# 另一个终端
gdb kernel.elf
  (gdb) target remote :1234
  (gdb) b kernel_main
  (gdb) c
  (gdb) info registers
  (gdb) x/10x 0x100000   # 检查内存
```

### 实机调试三板斧

当 QEMU 上正常但实机上 crash 时：

1. **二分注释法**：注释掉一半可疑代码，看实机是否崩溃
2. **VGA 埋点**：在关键路径上写 `printk("reached point A")`，在实机上看最后一条输出
3. **寄存器 dump**：在异常处理中打印所有寄存器值到 VGA，然后 `hlt`

### 常见实机异常

| 异常号 | 名称 | 常见原因 |
|--------|------|----------|
| #0  (#DE) | Divide Error | 除零 |
| #6  (#UD) | Invalid Opcode | 执行了数据区域 |
| #8  (#DF) | Double Fault | 异常处理中又发生异常（=内核逻辑坏了）|
| #13 (#GP) | General Protection | 段权限错误、非法地址 |
| #14 (#PF) | Page Fault | 缺页或页表配置错误 |

---

## 下一步

在开始 M1 之前，建议把 demo 的 `boot.bin` 烧到 U 盘上，在笔记本上确认能显示 "Hello OS!"。这一步验证了整条链路，会让你对后续开发充满信心。
