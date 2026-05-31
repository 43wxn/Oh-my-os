# Demo: Hello OS

## 这个 demo 做了什么

当你按下笔记本电源键后，CPU 执行的第一段代码就是这个 512 字节的 MBR（Master Boot Record）。Demo 截获了这个位置，在屏幕上打印 "Hello OS!"，证明从编译器到实机的整条链路是通的。

## 从按下电源到看到 "Hello OS!" 的完整流程

```
┌────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ 按下电源 │ ──▶ │ BIOS POST │ ──▶ │ 读 MBR   │ ──▶ │ 执行 MBR │
│ 按钮   │     │ 硬件自检  │     │ 到 0x7C00│     │ 我们的代码│
└────────┘     └──────────┘     └──────────┘     └──────────┘
                                                        │
                                            ┌───────────┘
                                            ▼
                                      ┌──────────┐
                                      │ VGA 显示 │
                                      │Hello OS! │
                                      └──────────┘
```

### 详细分解

**Step 1: 按下电源按钮**

主板通电，CPU 的 `CS:IP` 寄存器被硬件初始化为 `0xF000:0xFFF0`，指向 BIOS ROM 的入口。

**Step 2: BIOS POST（Power-On Self Test）**

BIOS 固件执行硬件自检：检查内存、初始化 VGA、枚举 PCI 设备、初始化键盘控制器（这就是 dmesg 里看到的 `i8042: PNP: PS/2 Controller`）。

**Step 3: BIOS 寻找启动设备**

BIOS 按照 CMOS 中设置的启动顺序（Boot Order）依次检查每个设备：USB-HDD → SATA HDD → PXE 网络。检查方法是读取该设备的 **第一个扇区**（512 字节），看最后两个字节是不是 `0x55` `0xAA`。

```
MBR 扇区的结构:
┌─────────────────────────────────────────────┐
│ 0x000 - 0x1BD   446 bytes   启动代码       │
│ 0x1BE - 0x1FD    64 bytes   分区表 (4×16B) │
│ 0x1FE - 0x1FF     2 bytes   魔数 0x55 0xAA │
└─────────────────────────────────────────────┘
```

如果 `0x55AA` 存在，BIOS 就把这 512 字节加载到物理地址 `0x7C00`，然后跳转过去。

> 为什么是 `0x7C00`？这是 IBM PC 5150（1981 年）的遗留约定。当时 IBM 的 BIOS 作者选了 `0x7C00`（位于 32KB 以下，不与其他 BIOS 数据结构冲突），之后所有兼容机都照做了。

**Step 4: CPU 开始执行我们的代码**

此时 CPU 处于 **16 位实模式**：
- 所有寄存器都是 16 位的
- 物理地址 = `段寄存器 × 16 + 偏移`（20 位地址，最多访问 1MB 内存）
- 没有内存保护，可以直接读写任何地址
- 可以调用 BIOS 中断服务

**Step 5: 我们的代码做了什么**

```asm
; 1. 初始化段寄存器 — 因为 BIOS 不保证它们的值
    xor ax, ax          ; ax = 0
    mov ds, ax          ; 数据段 = 0
    mov es, ax          ; 附加段 = 0
    mov ss, ax          ; 栈段 = 0
    mov sp, 0x7C00      ; 栈向 0x7C00 以下生长（不覆盖我们的代码）

; 2. 通过 BIOS 中断设置 VGA 文本模式
    mov ax, 0x0003
    int 0x10            ; INT 0x10, AH=0x00, AL=0x03 → 80×25 彩色文本

; 3. 用 BIOS 中断逐字符打印
    mov ah, 0x0E        ; INT 0x10, AH=0x0E → 电传模式（TTY）
    mov al, 'H'
    int 0x10

; 4. 停机
    cli                 ; 禁止中断
    hlt                 ; CPU 休眠
```

**Step 6: VGA 显示文字**

BIOS INT 0x10 服务内部做了两件事：
1. 在 VGA 文本模式显存（`0xB8000`）写入字符字节 + 属性字节
2. 更新光标位置

VGA 控制器持续扫描 `0xB8000` 的显存内容，驱动 LCD 面板显示对应像素。

---

## 实模式基础概念

### 内存分段

x86 从一开始就使用**分段**模型。在 16 位实模式下：

```
物理地址 = (段寄存器 << 4) + 偏移地址

示例:  0x07C0:0x0000 → 0x7C00
       0x0000:0x7C00 → 0x7C00  (同一个物理地址！)
```

BIOS 通常用 `0x0000:0x7C00` 跳转到 MBR，但某些 BIOS 用 `0x07C0:0x0000`。这就是我们第一步就初始化所有段寄存器的原因。

### 关键段寄存器

| 寄存器 | 含义 | 典型用途 |
|--------|------|----------|
| CS | Code Segment | 代码段，自动用于取指令 |
| DS | Data Segment | 数据段，`mov [addr], ax` 隐含使用 |
| ES | Extra Segment | 附加段，`mov [es:di], ax` 显式指定 |
| SS | Stack Segment | 栈段，`push/pop` 使用 |
| FS, GS | 386+ 增加的通用段寄存器 | 实模式下少用 |

### BIOS 中断向量表

实模式下，物理地址 `0x0000 - 0x03FF`（1KB）存放 256 个中断向量，每个 4 字节（段:偏移）。

```
0x0000: INT 0x00  (除零异常)
0x0004: INT 0x01  (单步)
    ...
0x0040: INT 0x10  (VGA/视频服务) ← 我们在 demo 里用的
0x0058: INT 0x16  (键盘服务)
0x004C: INT 0x13  (磁盘服务)
0x0080: INT 0x20  (DOS 兼容)
```

### INT 0x10 核心功能

| AH | 功能 | 参数 |
|----|------|------|
| 0x00 | 设置视频模式 | AL = 模式号 (0x03 = 80×25 彩色文本；0x13 = 320×200×256色) |
| 0x0E | 电传输出 | AL = 字符, BL = 颜色, BH = 页号 |
| 0x13 | 写字符串 | ES:BP = 字符串地址, CX = 长度 |

### VGA 显存直接访问

除了调 BIOS 中断，也可以直接写显存：

```c
// VGA 文本模式显存段: 0xB8000
// 每屏 80×25 = 2000 字符，每个字符占 2 bytes:
//   Byte 0: 字符 ASCII
//   Byte 1: 属性（bit3-0 前景色, bit7-4 背景色）

char *video = (char *)0xB8000;
video[0] = 'H';   // 行 0 列 0 的字符
video[1] = 0x0F;  // 白色前景, 黑色背景
```

在实模式下：`mov ax, 0xB800; mov es, ax; mov byte [es:0], 'H'` 等同于上面的 C 代码。

---

## 工具链说明

### 为什么用这些工具

```
boot.S   →   汇编为 .o     →    链接为 .elf    →    提取裸二进制
(源码)       (as --32)          (ld -T boot.ld)      (objcopy -O binary)
```

| 步骤 | 工具 | 做什么 |
|------|------|--------|
| `as --32` | GNU Assembler | 将 `.S` 汇编源代码翻译成可重定位目标文件 (`.o`) |
| `ld -T boot.ld` | GNU Linker | 按链接脚本把 `.o` 放到正确地址 (`0x7C00`)，解析符号 |
| `objcopy -O binary` | objcopy | 从 ELF 文件中剥出纯二进制——没有 header，没有 section table |

**为什么要 `objcopy`？**
BIOS 不理解 ELF 格式。它直接把 MBR 扇区的 512 个字节加载到 `0x7C00` 然后执行。我们的 `boot.bin` 必须是纯二进制——开头的第一个字节就是可执行指令，不是 ELF header。

### 链接脚本的作用

```ld
SECTIONS
{
    . = 0x7C00;        /* 告诉链接器：代码的基地址是 0x7C00 */
    .text : {
        *(.text)       /* 所有 .text 段放这里 */
    }
}
```

`boot.ld` 最关键的一句是 `. = 0x7C00`。没有它，链接器不知道我们的代码要被加载到哪个地址，所有标签（如 `msg`）的值都会错。

---

## QEMU vs 实机的区别

| | QEMU | 实机 (ASUS X542UF) |
|---|---|---|
| 冷启动 | `qemu-system-i386 -drive file=boot.bin,format=raw` | 上电 → BIOS → MBR |
| 启动设备 | 虚拟 IDE/Floppy 控制器 | 真实 SATA AHCI 控制器 |
| VGA 行为 | QEMU 模拟的 Bochs VGA | Intel UHD Graphics 620 (VGA 兼容模式) |
| MBR 位置 | 虚拟磁盘镜像的 sector 0 | U 盘 或 SATA 硬盘的 sector 0 (LBA 0) |

**QEMU 和实机对 MBR 来说是完全一样的**——因为 MBR 只用到 BIOS 中断和 VGA 显存，这两样 QEMU 都模拟得正确。这也是为什么 demo 不需要改动就能在两种环境运行。

---

## 从 demo 到完整内核的路线

```
demo 阶段 (现在):          MBR → 实模式 → 调 BIOS 中断 → 打印字符串 → hlt
                          512 字节纯汇编

下一个阶段 (bootloader):   MBR → 从磁盘加载内核 ELF → 切换保护模式 → 跳转
                          ~2KB 汇编 + C

再下一个阶段 (kernel):     C 内核入口 → 初始化 GDT/IDT/PIC/PIT → kmain
                          → 打印 "Kernel initialized" → 进入死循环等待中断
```

`demo/boot.S` 的代码会被吸收进 `boot/` 目录，成为正式 bootloader 的 MBR 部分。

---

## 快速参考

```bash
# 编译
cd demo && make

# QEMU 测试
make qemu

# 烧录 U 盘
sudo dd if=boot.bin of=/dev/sdX bs=512 count=1 conv=fsync

# 用 hexdump 检查 MBR
hexdump -C boot.bin | tail -5        # 应该看到 ...55 aa
```

## 参考资料

- [OSDev Wiki - Boot Sequence](https://wiki.osdev.org/Boot_Sequence) — x86 启动流程
- [OSDev Wiki - MBR (x86)](https://wiki.osdev.org/MBR_(x86)) — MBR 结构详解
- [OSDev Wiki - Real Mode](https://wiki.osdev.org/Real_Mode) — 实模式编程
- [Ralf Brown's Interrupt List](https://www.ctyme.com/rbrown.htm) — BIOS 中断大全
- [VGA 文本模式编程](https://wiki.osdev.org/Text_UI) — 直接写显存
- [Ian Seyler - Let's Write a Minimal x86 Bootloader](http://3zanders.co.uk/2017/10/13/writing-a-bootloader/) — 最小 bootloader 教程
