# M1 阶段文档: Bootloader + 保护模式 + 内核入口

> 日期: 2026-05-31
> 状态: ✅ 完成
> 实机验证: ASUS X542UF 通过

## 一、阶段目标

实现 x86 从 BIOS 上电到 C++ 内核主函数的完整启动链。

## 二、成果

实机 VGA 显示:

```
+------------------------------------------+
|  Oh-my-os Kernel v0.0.1                  |
+------------------------------------------+

  [M1] Bootloader + Protected Mode  : OK

  CPU   : Intel i5-8250U @ 1.60GHz
  Arch  : x86 32-bit Protected Mode
  Load  : 0x100000 (1MB)

  [M2] Interrupt System (PIC+IDT+PIT)
        ...coming next

+------------------------------------------+
  System halted.
```

## 三、启动流程

```
BIOS POST
  │ INT 0x19 → 读启动设备 Sector 0 (LBA 0) → 检查 0x55AA
  │ 加载到 0x7C00
  ▼
MBR (boot/mbr.S, 512B)
  │ 规范化 CS:IP, 设置段寄存器
  │ INT 0x13 AH=0x42 → 从 LBA 1 读 7 扇区到物理 0x500
  │ LJMP 0x0050:0x0000
  ▼
Stage2 实模式部分 (boot/stage2.S)
  │ 保存启动盘号, 打印 banner
  │ 开启 A20 地址线 (三级 fallback)
  │ 从 LBA 8 读 32 扇区(内核)到 0x8000
  │ 校验内核数据非零
  │ LGDT + CR0.PE=1 + LJMP $0x08
  ▼
Stage2 保护模式部分
  │ 设置段选择子 + 栈 (0x90000)
  │ REP MOVSL: 内核 0x8000 → 0x100000 (1MB)
  │ VGA 打印 "Entered protected mode"
  │ CALL 0x100000
  ▼
内核入口 (kernel/arch/x86/boot.S)
  │ 设置内核栈 (16KB)
  │ 清零 BSS 段
  │ 调用 .init_array (C++ 全局构造函数)
  │ CALL kernel_main()
  ▼
kernel_main() (kernel/kernel.cc)
  │ 清屏 → printk banner → 停机
```

## 四、文件清单

| 文件 | 行数 | 职责 |
|------|------|------|
| `boot/mbr.S` | ~110 | MBR 引导扇区, INT 0x13 扩展读加载 Stage2 |
| `boot/mbr.ld` | 11 | MBR 链接脚本 (基址 0x7C00) |
| `boot/stage2.S` | ~400 | 实模式→保护模式, A20/GDT/内核加载/校验 |
| `boot/boot.ld` | 14 | Stage2 链接脚本 (基址 0x500) |
| `kernel/arch/x86/boot.S` | ~70 | 内核入口, 栈/BSS/C++初始化 |
| `kernel/kernel.cc` | ~35 | 内核主函数, VGA 输出 |
| `kernel/lib/printk.cc` | ~120 | 格式化输出 (VGA 显存直接写) |
| `include/stdint.h` | ~30 | 基础类型定义 |
| `include/kernel/printk.h` | ~25 | printk 接口声明 |
| `scripts/link.ld` | ~55 | 内核链接脚本 (基址 1MB) |
| `Makefile` | ~130 | M1 构建系统 |

## 五、关键技术决策

### 5.1 Stage2 加载地址: 0x500

**问题:** 最初设计 Stage2 加载到物理 0x10000，链接在 0x0000，用 DS=0x1000 定位。这导致 GDT/LGDT 中的地址是链接地址(0x00000xxx)而非物理地址(0x1000x)，保护模式跳转后 CPU 读到错误地址 → 三重故障。

**解决:** 改为加载到物理 0x500，链接基址 = 0x500，DS=0。**链接地址 = 物理地址**，所有符号无需运行时修正。0x500 选择理由:
- BIOS 数据区 (0x000-0x4FF) 之上
- MBR (0x7C00) 之下
- 约 30KB 可用空间，Stage2 只需 3KB

### 5.2 A20 三级 Fallback

实机 A20 开启不可靠，单一方法可能失败。实现三级级联:

```
BIOS INT 0x15 AX=0x2401 → 失败 →
键盘控制器 (8042 port 0x64) → 失败 →
Fast A20 (System Control Port A, 0x92)
```

每种方法后都有验证: 写 0x100000 与读 0x000000 对比，确保 A20 真正打开。

### 5.3 保护模式前读盘，保护模式后复制

BIOS INT 0x13 不可在保护模式下使用(是实模式中断)。内核必须在实模式下读取到临时缓冲区(<1MB)，保护模式切换后再复制到目标地址(1MB)。

```
实模式: INT 0x13 → 内核扇区 → 临时缓冲 0x8000
保护模式: REP MOVSL → 0x8000 → 0x100000
```

### 5.4 GDT 平坦模型

```c
GDT[0]: NULL
GDT[1]: Code segment 0x08  // Base=0, Limit=4GB, DPL=0, 32-bit
GDT[2]: Data segment 0x10  // Base=0, Limit=4GB, DPL=0, 32-bit
```

Base=0 + Limit=4GB = 平坦模型。保护模式下段保护机制本质冗余(页表替代了它)，但我们仍需 GDT 满足硬件要求。

### 5.5 C++ 内核

- `extern "C"` 用于汇编可见的入口函数 (kernel_main)
- `-fno-exceptions -fno-rtti`: 禁用异常和 RTTI
- `.init_array` 段: 全局对象构造函数在内核启动时显式调用
- 手动提供 `operator new`/`delete` (后续 kmalloc/kfree 对接)

## 六、实机调试踩坑

| 现象 | 根因 | 修复 |
|------|------|------|
| Stage2 日志闪现后重启 | GDT 基址错误(链接地址≠物理地址) | Stage2 加载到 0x500, 链接基址=物理地址 |
| 磁盘读失败 "ERR" | Stage2 硬编码盘号 0x80, 覆盖 MBR 传来的值 | 保存 DL 到 boot_drive 变量 |
| 进入 PM 瞬间重启 | GDT 条目 base=0, 但代码在 0x10000 | 整体搬到 0x500, base=0 正确指向代码 |
| 屏幕显示乱码 | 清屏用 NULL(0x00) 而非空格(0x20) | `0x0F00` → `0x0F20` |

## 七、内存布局 (实机 8GB)

```
0x00000000 - 0x000004FF  BIOS 中断向量表 + BDA (1.25KB)
0x00000500 - 0x000010FF  Stage2 代码 (3KB)
0x00004000 - 0x00007FFF  内核临时缓冲区 (16KB)
0x00007C00 - 0x00007DFF  MBR (512B)
0x00008000 - 0x0000FFFF  可用
0x00090000 - 0x0009FFFF  内核栈 (64KB)
0x000A0000 - 0x000BFFFF  VGA 显存 (128KB)
0x00100000+              内核代码/数据/堆 (1MB+)
...
0xFFFFFFFF               8GB top
```

## 八、下一步 (M2)

中断系统: IDT (256 门) + 8259A PIC + PIT (100Hz) + PS/2 键盘 + int 0x80 系统调用框架。
