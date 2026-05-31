# Oh-my-os 需求规格说明书

## 一、项目概述

从零实现一个可在真实硬件上运行的单用户命令行操作系统。不基于任何现有内核（Linux/BSD），完全自研。最终运行在华硕 VivoBook 15 X542UF 笔记本上，能够运行第三方开源软件（vim、gcc、curl）。

### 1.1 目标硬件

| 项目 | 规格 |
|------|------|
| 型号 | ASUS VivoBook 15 X542UF |
| CPU | Intel Core i5-8250U @ 1.60GHz（4 核 8 线程，Kaby Lake R） |
| 内存 | 8 GB DDR4（实际可用 7.7GB） |
| 硬盘 | 111.8 GB SATA SSD，AHCI 模式 |
| 显卡 | Intel UHD Graphics 620（集成）+ NVIDIA GeForce MX130（独立） |
| 固件 | Legacy BIOS（非 UEFI） |
| 键盘 | PS/2 AT Translated Set 2（端口 0x60/0x64，IRQ 1） |
| 有线网卡 | Realtek RTL8111（PCI 02:00.0） |
| 无线网卡 | Qualcomm Atheros AR9565（暂不驱动） |

### 1.2 开发环境

- **开发机**：台式机 Linux（编写代码 + 交叉编译）
- **目标机**：上述华硕笔记本（实机运行）
- **模拟器**：QEMU（快速迭代验证，最终仍需实机验证）
- **工具链**：nasm + i686-elf-g++ + ld + objcopy（初始阶段），i686-linux-musl-g++（用户态阶段）

### 1.3 核心设计原则

**原则 A：POSIX 兼容的系统调用接口**

系统调用编号、参数、返回值语义兼容 Linux i386 ABI（`int 0x80`，eax=调用号，ebx/ecx/edx/esi/edi/ebp=参数）。未实现的调用返回 `-ENOSYS`。这确保 musl libc 无需修改即可运行，进而支撑第三方软件移植。

**原则 B：先自写 libc 验证，最终移植 musl**

- 开发阶段（M2-M6）：自写简化 libc（~500 行）快速验证系统调用链路
- 完成阶段（M7 后）：移植 musl，获得完整 C11 标准库，支撑 vim/gcc/curl 等真实软件

**原则 C：一切皆文件的 VFS 抽象**

磁盘文件、设备节点（/dev/tty、/dev/sda）、管道、网络 socket 统一为文件描述符，通过 `struct file_ops`（read/write/ioctl/close/seek）操作。Shell 不感知 fd 底层是什么资源。

---

## 二、里程碑总览

```
M0 (✅)        M1              M2              M3              M4
Demo          Bootloader      中断系统        内存管理         进程管理
512B MBR      保护模式切换     PIC+PIT+键盘    页表+kmalloc    PCB+调度+同步

M5              M6              M7              M8              M9
AHCI 磁盘驱动   文件系统        用户态+Shell    IPC+管道        网络栈
ATA 读写        FAT32+VFS       ELF+libc       pipe+dup2       TCP/IP+RTL8111

M10             M11
ACPI 电源管理   SMP 多核
关机+重启       4核8线程
```

| 阶段 | 内容 | 实机验证标准 |
|------|------|-------------|
| M0 | 512B MBR 输出 "Hello OS!" 到 VGA | ✅ 已完成 |
| M1 | Bootloader：MBR→Stage2→保护模式→加载 kernel | VGA 显示 "Entered protected mode" |
| M2 | IDT + 8259A PIC + PIT + PS/2 键盘 + 系统调用框架 | 按键显示对应字符，时钟 tick 计数 |

---

## 三、各模块功能需求

### 3.1 系统启动（M0-M1）

**功能：**
- MBR（512B）：含分区表 + 激活标志，BIOS 加载到 0x7C00
- Stage2：A20 门、GDT、实模式→32 位保护模式、从 FAT32 分区加载 kernel ELF
- 输出启动日志到 VGA 文本模式（80×25，0xB8000）
- 兼容 GRUB chainload（标准 MBR 分区表）

**技术指标：**
- 启动成功率 100%（非"有时候能启动有时候不能"）
- 支持 Legacy BIOS，CSM 模式

### 3.2 中断与异常处理（M2）

**功能：**
- IDT：256 个中断门，前 32 个 CPU 异常 + IRQ 0-15 + 系统调用门（int 0x80）
- 8259A PIC：主片 IRQ0-7 → 0x20-0x27，从片 IRQ8-15 → 0x28-0x2F
- PIT 8253：时钟中断 100Hz（`1193180 / 100`）
- PS/2 键盘：IRQ1 → 端口 0x60 → 扫描码 → 环形缓冲区 → 转 ASCII
- 系统调用：`int 0x80`，遵循 Linux i386 ABI

**技术指标：**
- 时钟中断频率误差 <1%（实机）
- 系统调用优先实现：`write`(4), `exit`(1), `read`(3)

### 3.3 内存管理（M3）

**功能：**
- 物理内存探测：BIOS E820 map → 识别可用区域（>1MB）
- 物理页框分配器：位图或伙伴算法，`alloc_page()` / `free_page()`
- 二级页表（10-10-12）：`map_page(vaddr, paddr, flags)`
- 内核堆：`kmalloc()` / `kfree()`（基于页框分配器）
- 按需分页（Demand Paging）：缺页异常 #14 → 分配物理页 → 映射
- POSIX `brk`(45) 和 `mmap`(90) 系统调用

**技术指标：**
- 管理全部 8GB 物理内存
- 4KB 页大小
- 内存分配无泄漏，支持释放后重用

### 3.4 进程管理（M4）

**功能：**
- PCB：PID、状态、上下文（寄存器快照）、页表 CR3、文件描述符表
- 上下文切换：`switch_to(prev, next)` 汇编实现
- 调度器：Round Robin（PIT 时钟驱动，默认 10ms 时间片），FCFS 可选
- 进程状态：就绪/运行/阻塞/僵尸
- 同步原语：信号量（sem_wait/sem_post）、互斥锁（mutex_lock/unlock）
- POSIX 系统调用：`clone`(120) = fork 底层, `execve`(11), `exit`(1), `wait`, `getpid`(20)

**技术指标：**
- 支持 ≥32 个并发进程
- 进程切换开销 <1ms（实机）
- 父子进程关系树正确

### 3.5 磁盘驱动（M5）

**功能：**
- PCI 枚举：扫描 Class Code 0x010601，获取 ABAR
- AHCI HBA 初始化与 Port 管理
- ATA IDENTIFY：获取磁盘型号、序列号、LBA48 支持、容量
- ATA READ DMA EXT / WRITE DMA EXT：LBA48 寻址
- PRDT + Command Table + FIS 构造
- 块设备抽象接口：`block_read(lba, buf, count)` / `block_write(lba, buf, count)`

**技术指标：**
- 成功识别并读写 111.8GB SATA SSD
- 磁盘读写正确率 100%
- 支持 LBA48 全盘寻址

**参考资料：** [docs/ahci.md](docs/ahci.md)

### 3.6 文件系统（M6）

**功能：**
- VFS 层：`struct file` + `struct file_ops`（read/write/ioctl/close/seek）
- FAT32：解析 VBR BPB → 加载 FAT 表 → 目录遍历 → 文件读写 → 文件创建/删除
- 文件描述符表：每进程独立的 fd table，POSIX `open`(5)/`close`(6)/`read`(3)/`write`(4)
- 路径解析：绝对路径 + 相对路径
- 设备文件：`/dev/tty`（键盘+屏幕）、`/dev/sda`（块设备）、`/dev/null`
- RAMFS：开发阶段不依赖磁盘的内存文件系统
- POSIX `ioctl`(54) 用于设备控制

**技术指标：**
- 目录嵌套 ≥4 层
- 单文件支持 ≥16MB
- 重启后文件持久存在（FAT32 磁盘）

### 3.7 显示与输入驱动

**VGA 显示：**
- 文本模式 80×25（端口 0x3D4/0x3D5，显存 0xB8000）
- `printk` 内核格式化输出
- 光标控制、滚屏、颜色属性

**PS/2 键盘：**
- 端口 0x60（数据）/ 0x64（命令/状态）
- IRQ1 中断处理
- AT Set 2 扫描码 → ASCII，US QWERTY 布局
- Shift/Ctrl/Caps Lock 组合键
- 256 字节环形缓冲区

### 3.8 用户态与 Shell（M7）

**功能：**
- Ring 3 切换：设置用户态段选择子（RPL=3），`iret` 到用户态
- ELF 加载器：解析 ELF header → 分配页表 → 映射 segments → 初始化用户栈
- 自写简化 libc：printf/malloc/free/string + `int 0x80` 包装
- init 进程（PID 1）：挂载文件系统 → 启动 shell
- Shell 内建命令：`cd`, `exit`, `help`
- 外部命令（从 `/bin/` 加载 ELF）：`ls`, `cat`, `echo`, `ps`, `kill`
- POSIX 系统调用：`execve`(11)

**技术指标：**
- Shell 交互响应 <100ms
- 至少 5 个用户态程序可运行
- 用户程序崩溃不影响内核

### 3.9 IPC 与管道（M8）

**功能：**
- 管道：`pipe()`(42) 创建环形缓冲区（4KB），返回读写两个 fd
- 重定向：`dup2()`(63) 复制 fd 到指定位置
- Shell 管道语法：`cmd1 | cmd2`（pipe + fork + dup2 + exec）
- Shell 重定向语法：`cmd > file`, `cmd < file`
- 信号：`kill`(37)、SIGKILL、SIGTERM、SIGINT（Ctrl-C 触发）

**技术指标：**
- `cat /etc/motd | wc -l` 输出正确
- `ls > /tmp/list.txt` 后文件内容正确

### 3.10 网络栈（M9）

**功能：**
- RTL8111 以太网卡驱动（PCI MMIO + 描述符环 + DMA）
- ARP 协议（IP ↔ MAC 地址解析）
- IPv4：头部构造/解析/校验和/分片重组
- ICMP：Echo Reply（ping 响应）
- TCP：三次握手/滑动窗口/确认重传/连接关闭（简化版）
- UDP（可选）：DNS 查询 + DHCP 获取 IP
- POSIX `socketcall`(102)：socket / bind / listen / accept / connect / send / recv
- Socket 集成进 VFS：通过文件描述符访问

**技术指标：**
- `ping 192.168.1.1` 收到 reply
- `wget http://example.com` 下载 HTML
- TCP 吞吐 ≥1MB/s（局域网）

### 3.11 电源管理（M10）

**功能：**
- ACPI 表解析：RSDP → RSDT → FADT
- 关机（S5）：写 PM1a_CNT 端口
- 重启：8042 键盘控制器写 0xFE，或 ACPI reset_reg
- 电源按钮事件：ACPI Fixed Event → SIGTERM 广播 → sync → 关机

**技术指标：**
- `shutdown` 命令正常关机
- `reboot` 命令正常重启

### 3.12 SMP 多核（M11）

**功能：**
- ACPI MADT 解析：枚举 Local APIC 条目
- Local APIC 初始化：MMIO（0xFEE00000）、Timer 校准
- AP 启动：BSP 发 INIT-SIPI-SIPI → AP 实模式起步 → 切保护模式 → 进 C
- 每核独立内核栈 + 独立 PCB/TSS
- 调度器 SMP 化：每核 runqueue + 负载均衡
- 内核锁：大内核锁起步，逐步细化
- TLB shootdown：IPI 通知其他核 `invlpg`

**技术指标：**
- 8 个逻辑核全部参与调度
- 8 个死循环进程各占满一个核（100% 利用率）

---

## 四、系统调用接口（Linux i386 ABI 兼容）

| 编号 | 名称 | 说明 | 实现阶段 |
|------|------|------|----------|
| 1 | exit | 进程退出 | M4 |
| 3 | read | 读文件描述符 | M6 |
| 4 | write | 写文件描述符 | M2 |
| 5 | open | 打开文件 | M6 |
| 6 | close | 关闭文件描述符 | M6 |
| 11 | execve | 执行程序 | M7 |
| 20 | getpid | 获取进程 PID | M4 |
| 37 | kill | 发送信号 | M8 |
| 41 | dup | 复制文件描述符 | M8 |
| 42 | pipe | 创建管道 | M8 |
| 45 | brk | 扩展进程堆 | M3 |
| 54 | ioctl | 设备控制 | M6 |
| 63 | dup2 | 复制 fd 到指定编号 | M8 |
| 90 | mmap (old) | 内存映射 | M3 |
| 91 | munmap | 解除映射 | M3 |
| 102 | socketcall | socket 操作（多路复用） | M9 |
| 120 | clone | 创建进程 | M4 |
| 252 | exit_group | 线程组退出 | M11 |

未实现的调用返回 `-ENOSYS`。总计目标实现约 50 个系统调用（覆盖 musl 所需的最小集）。

---

## 五、C/C++ 标准库路线

| 阶段 | 方案 | 用途 |
|------|------|------|
| M2-M6 | 自写简化 libc（~500 行） | 快速验证系统调用链路 |
| M7 后 | 移植 musl（完整 C11 + POSIX）+ C++ 运行时 | 支撑第三方软件（vim/g++/curl） |

**musl 移植要求：**
- 内核提供 musl 所需的约 50 个 Linux 系统调用
- 支持静态链接（`i686-linux-musl-g++ -static`）
- 初期不实现动态链接
- 进程地址空间布局兼容 Linux（代码段/数据段/堆/mmap 区域）

---

## 六、VFS 统一抽象

```
struct file_ops {
    int  (*read)   (struct file *f, void *buf, size_t n);
    int  (*write)  (struct file *f, const void *buf, size_t n);
    int  (*ioctl)  (struct file *f, int cmd, void *arg);
    int  (*close)  (struct file *f);
    int  (*seek)   (struct file *f, off_t offset, int whence);
};
```

所有 I/O 资源通过同一接口访问：

| 资源 | 访问路径 | 后端 |
|------|----------|------|
| FAT32 磁盘文件 | `/home/foo.txt` | FAT32 inode |
| 终端（键盘+屏幕） | `/dev/tty` | 键盘环形缓冲区 + VGA 显存 |
| 块设备 | `/dev/sda` | AHCI 块设备层 |
| 管道 | `pipe()` 返回的匿名 fd | 环形缓冲区 |
| Socket | `socket()` 返回的 fd | TCP/UDP 协议栈 |

---

## 七、最终验证标准

M11 完成后，以下操作应在实机上完整通过：

```bash
# 1. 开机启动
# → VGA 显示 boot log → ohm$ 提示符

# 2. 基本文件操作
ohm$ ls /bin
ohm$ cat /etc/motd
ohm$ echo "hello" > /tmp/test.txt
ohm$ cat /tmp/test.txt

# 3. 管道与重定向
ohm$ cat /etc/motd | wc -l
ohm$ ps | grep init

# 4. 进程管理
ohm$ ps
ohm$ kill 3

# 5. 网络
ohm$ ping 192.168.1.1
ohm$ wget http://example.com

# 6. 电源管理
ohm$ shutdown    # 笔记本断电

# 7. 第三方软件（用 musl 交叉编译）
ohm$ /bin/vim /home/hello.txt     # 编辑文件
ohm$ /bin/g++ -o /tmp/a.out test.cc  # 编译程序
ohm$ /bin/curl -o /tmp/index.html http://example.com  # 下载
```

---

## 八、架构总图

```
┌──────────────────────────────────────────────────────────┐
│  用户态 (Ring 3)                                          │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌──────────────────┐   │
│  │  vim   │ │  g++   │ │  curl  │ │ /bin/ls/cat/...  │   │
│  └───┬────┘ └───┬────┘ └───┬────┘ └────────┬─────────┘   │
│      └──────────┴──────────┴───────────────┘             │
│                       │ musl libc                        │
│                       │ int 0x80 (Linux ABI)              │
├───────────────────────┼──────────────────────────────────┤
│  内核态 (Ring 0)       │                                   │
│  ┌─────────────────────┼─────────────────────────────┐   │
│  │  系统调用层          │                              │   │
│  │  ┌──────────────────┴───────┐                      │   │
│  │  │       VFS (一切皆文件)    │                      │   │
│  │  ├────┬────┬────┬────┬─────┤                      │   │
│  │  │FAT32│/dev│pipe│socket│ ...│                    │   │
│  │  ├────┴────┴────┴────┴─────┤                      │   │
│  │  │      块设备层            │                      │   │
│  │  ├─────────────────────────┤                      │   │
│  │  │  AHCI 驱动  │  RTL8111  │  PS/2  │  VGA       │   │
│  │  ├─────────────┴───────────┴────────┴────────────┤   │
│  │  │  进程管理 (PCB + 调度器 + SMP)                  │   │
│  │  │  内存管理 (页表 + kmalloc + 按需分页)            │   │
│  │  │  中断管理 (PIC + IDT + PIT)                    │   │
│  │  └────────────────────────────────────────────────┘   │
│  └─────────────────────────────────────────────────────── │
├──────────────────────────────────────────────────────────┤
│  硬件                                                     │
│  i5-8250U │ 8GB RAM │ SATA SSD │ RTL8111 │ PS/2 │ VGA    │
└──────────────────────────────────────────────────────────┘
```
