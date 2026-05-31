# Oh-my-os

从零实现的 x86 操作系统内核，最终运行在华硕 VivoBook 15 X542UF 实机上。

## 硬件目标

| 项目 | 规格 |
|------|------|
| 型号 | ASUS VivoBook 15 X542UF |
| CPU | Intel Core i5-8250U @ 1.60GHz (Kaby Lake R) |
| 架构 | x86_64（内核运行于 32 位保护模式） |
| 内存 | 8 GB DDR4 |
| 硬盘 | 111.8 GB SATA SSD (AHCI) |
| 显卡 | Intel UHD Graphics 620 + NVIDIA MX130 |
| 固件 | Legacy BIOS |
| 键盘 | PS/2 (AT Translated Set 2) |

## 特性

- [ ] 自定义 MBR Bootloader（实模式 → 保护模式）
- [ ] 中断管理（8259A PIC + IDT）
- [ ] 内存管理（物理页框分配器 + 二级页表虚拟内存）
- [ ] 进程管理（PCB + FCFS/RR 调度 + 信号量/互斥锁）
- [ ] AHCI 磁盘驱动（SATA SSD 读写）
- [ ] 文件系统（RAMFS 开发用 + FAT32 实机持久化）
- [ ] ELF 用户程序加载
- [ ] Shell 命令行解释器

## 项目结构

```
Oh-my-os/
├── boot/                   # ① 启动引导
│   ├── boot.asm            #    MBR 启动扇区 (512B, BIOS→0x7C00)
│   ├── loader.asm          #    第二阶段加载器 (实模式→保护模式, 加载内核)
│   └── multiboot.asm       #    Multiboot 头 (兼容 GRUB)
│
├── kernel/                 # ② 内核源码
│   ├── kernel.cc           #    内核主入口 kernel_main()
│   ├── panic.cc            #    内核 Panic 处理
│   │
│   ├── arch/x86/           #    架构相关 (直接操作 CPU/寄存器)
│   │   ├── boot.asm        #      入口 _start, 设栈, 调 kernel_main
│   │   ├── gdt.cc          #      全局描述符表 (段选择子)
│   │   ├── idt.cc          #      中断描述符表 (异常+IRQ+系统调用)
│   │   ├── isr.cc          #      中断服务例程分发
│   │   ├── pic.cc          #      8259A PIC (初始化/重映射/EOI)
│   │   ├── pit.cc          #      8253/8254 PIT (时钟中断)
│   │   ├── paging.cc       #      二级页表 (4KB 页, 控制寄存器CR0/CR3)
│   │   ├── syscall.cc      #      int 0x80 系统调用总入口
│   │   └── port.asm        #      I/O 端口读写 (inb/outb/...)
│   │
│   ├── mm/                 #    内存管理
│   │   ├── pmm.cc          #      物理内存 (BIOS E820 探测, 页框分配/释放)
│   │   ├── vmm.cc          #      虚拟内存 (页表映射, 地址空间)
│   │   ├── heap.cc         #      内核堆 (kmalloc/kfree)
│   │   └── page.cc         #      伙伴算法 or 位图页分配器
│   │
│   ├── proc/               #    进程管理
│   │   ├── pcb.cc          #      PCB 数据结构, PID 分配
│   │   ├── scheduler.cc    #      调度器 (FCFS / Round Robin)
│   │   ├── switch.asm      #      上下文切换 (保存/恢复寄存器)
│   │   ├── sync.cc         #      信号量 & 互斥锁
│   │   ├── fork.cc         #      fork() 系统调用
│   │   └── exec.cc         #      exec() — ELF 加载
│   │
│   ├── fs/                 #    文件系统
│   │   ├── vfs.cc          #      虚拟文件系统层 (统一操作接口)
│   │   ├── ramfs.cc        #      内存文件系统 (开发/调试用)
│   │   ├── fat32.cc        #      FAT32 实现 (实机持久化)
│   │   ├── fd.cc           #      文件描述符表 (每进程)
│   │   └── path.cc         #      路径解析 (绝对/相对)
│   │
│   ├── drivers/             #    硬件驱动
│   │   ├── ahci/            #    SATA AHCI 磁盘驱动
│   │   │   ├── ahci.cc      #      HBA 初始化, Port 管理
│   │   │   ├── pci.cc       #      PCI 总线扫描, 找 AHCI 设备
│   │   │   ├── ata.cc       #      ATA 命令封装 (IDENTIFY/READ/WRITE DMA)
│   │   │   └── fis.cc       #      FIS 构造与解析
│   │   ├── vga/             #    VGA 显示
│   │   │   ├── vga.cc       #      文本模式 (80×25, 0x3D4/0xB8000)
│   │   │   └── vesa.cc      #      VESA/VBE 图形 (可选)
│   │   └── keyboard/        #    PS/2 键盘
│   │       ├── keyboard.cc  #      PS/2 控制器 (0x60/0x64, IRQ1)
│   │       └── keymap.cc    #      扫描码→ASCII 映射表
│   │
│   └── lib/                #    内核基础库
│       ├── string.cc        #      strlen/strcmp/memcpy/memset/...
│       ├── printf.cc        #      printk 格式化输出
│       └── klib.cc          #      杂项工具
│
├── user/                   # ③ 用户态程序
│   ├── libc/               #    简化版 C/C++ 库
│   │   ├── stdio.cc        #      printf/putchar/gets
│   │   ├── stdlib.cc       #      malloc/free/atoi/exit
│   │   ├── string.cc       #      strlen/strcmp/memcpy
│   │   └── syscall.asm     #      int 0x80 包装函数
│   ├── shell/              #    Shell 命令行
│   │   ├── shell.cc        #      REPL 主循环
│   │   ├── parser.cc       #      命令分词
│   │   └── builtin.cc      #      内建命令 (cd/exit/help)
│   ├── init/               #    PID 1 init 进程
│   │   └── init.cc         #      挂载 FS, 启动 shell
│   └── programs/           #    可执行程序
│       ├── ls.cc            #      ls — 列出目录
│       ├── cat.cc           #      cat — 显示文件
│       ├── echo.cc          #      echo — 输出文本
│       ├── ps.cc            #      ps — 进程列表
│       └── kill.cc          #      kill — 终止进程
│
├── include/                # ④ 全局头文件
│   ├── kernel/             #    内核头 (gdt.h, idt.h, ...)
│   └── libc/               #    用户态头 (stdio.h, stdlib.h, ...)
│
├── scripts/                # ⑤ 构建脚本
│   ├── link.ld             #    链接脚本 (内核内存布局)
│   └── mkfs.sh             #    文件系统镜像制作
│
├── tools/                  # ⑥ 宿主工具 (在开发机上编译)
│   └── mkfs.cc             #    mkfs 工具 — 创建 FAT32 镜像
│
├── docs/                   # ⑦ 技术文档
│   └── ahci.md             #    AHCI 规范 & 驱动设计
│
├── Makefile                #    顶层构建
├── README.md               #    本文件
└── require.md              #    需求规格说明书
```

### 各目录详细说明

#### ① `boot/` — 启动引导
系统上电后 BIOS 执行的第一段代码。包含 512 字节的 MBR 启动扇区（需以 0x55AA 结尾）和第二阶段加载器。负责：实模式→保护模式切换、A20 门开启、GDT 加载、内核镜像从磁盘读到 1MB 处。是一个独立的迷你程序，最终会被 `dd` 写入磁盘的第一个扇区。

#### ② `kernel/` — 内核核心
内核的全部源代码，按子系统分层：

| 子目录 | 职责 | 语言 |
|--------|------|------|
| `arch/x86/` | 直接操作 x86 硬件：CPU 寄存器、GDT/IDT、PIC/PIT、页表 | C++ + 少量 asm |
| `mm/` | 物理内存探测与分配、虚拟地址空间管理、堆分配 | C++ |
| `proc/` | 进程生命周期 (创建/调度/退出)、同步原语 | C++ + asm (上下文切换) |
| `fs/` | 虚拟文件系统接口、文件描述符、RAMFS/FAT32 实现 | C++ |
| `drivers/` | 外设驱动：SATA AHCI 磁盘、VGA 显示、PS/2 键盘 | C++ |
| `lib/` | 内核内部使用的工具函数，无外部依赖 | C++ |

#### ③ `user/` — 用户态
运行在 Ring 3 的代码，不能直接访问硬件，所有资源通过系统调用获取。`libc/` 是连接用户程序和内核的桥梁。`programs/` 是最终用户在 shell 里能执行的命令。

#### ④ `include/` — 头文件
全局搜索路径（Makefile 中 `-Iinclude`）。分为内核态（`kernel/`）和用户态（`libc/`）两部分，两边 API 不同，编译时按目标选择。

#### ⑤ `scripts/` — 构建脚本
`link.ld` 最重要——它定义了最终内核 ELF 文件的段布局（.text 在哪、.bss 在哪、堆和栈的起始地址）。任何内存布局的修改都要先看这个文件。

#### ⑥ `tools/` — 宿主工具
在开发机（Linux）上编译运行的辅助程序，不是 OS 的一部分。`mkfs` 工具用于把一个空文件初始化为 FAT32 格式的文件系统镜像（含根目录、引导扇区参数块），供内核挂载。

#### ⑦ `docs/` — 文档
技术笔记和设计文档，与 `require.md`（需求）分开。`ahci.md` 已写完，后续可加入内存布局图、系统调用表等。

## 构建 & 运行

```bash
# 安装交叉编译工具链
sudo apt install nasm g++-multilib

# 编译
make

# QEMU 测试
make qemu

# 烧录到 U 盘
make write-usb USB=/dev/sdX
```

## 开发路线

| 阶段 | 内容 | 状态 |
|------|------|------|
| M1 | Bootloader + VGA "Hello World" | 待开始 |
| M2 | 中断系统 (PIC + IDT + PIT + PS/2) | 待开始 |
| M3 | 内存管理 (物理 + 虚拟) | 待开始 |
| M4 | 进程管理 + 调度 | 待开始 |
| M5 | AHCI 磁盘驱动 + 文件系统 | 待开始 |
| M6 | 用户态 + ELF 加载 + Shell | 待开始 |
| M7 | 实机适配与集成测试 | 待开始 |

## 参考资料

- [OSDev Wiki](https://wiki.osdev.org/)
- [AHCI Specification](https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html)
- [Intel 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
