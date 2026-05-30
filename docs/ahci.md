# AHCI 技术文档

> 目标实机：Intel Sunrise Point-LP SATA Controller [AHCI mode]
> PCI 位置：Bus 0, Device 0x17, Function 0，Class Code 0x010601

---

## 一、AHCI 概述

AHCI（Advanced Host Controller Interface）是 SATA 控制器的标准编程接口，由 Intel 定义。与传统的 IDE PIO（`inb`/`outb` 操作固定 I/O 端口）不同，AHCI 通过 **PCI 内存映射 I/O（MMIO）** 与主机通信。

```
传统 IDE PIO:  inb/outb 操作固定 I/O 端口 (0x1F0-0x1F7)
                    vs
AHCI SATA:     读写内存地址 (MMIO)，通过描述符链表发命令
```

AHCI 支持的关键能力：
- 最多 32 个设备端口
- 每个端口最多 32 个并发命令槽位
- 命令描述符链表 + PRDT（Physical Region Descriptor Table）实现 DMA 散列/聚集
- FIS（Frame Information Structure）标准化数据传输
- Native Command Queuing（NCQ，支持乱序命令执行）

---

## 二、关键寄存器与数据结构

### 2.1 内存布局总览

```
PCI BAR[5] (ABAR)
    │
    └── HBA Memory Registers (通用控制寄存器区)
           │  GHC (Global HBA Control)
           │  CAP (Capabilities)
           │  PI  (Ports Implemented)
           │  ...
           │
           └── Port[n] Registers (每个 Port 独立)
                  PxCMD  (Command & Status)
                  PxCI   (Command Issue)
                  PxIS   (Interrupt Status)
                  PxIE   (Interrupt Enable)
                  PxTFD  (Task File Data)
                  PxSIG  (Device Signature)
                  PxSERR (SATA Error)
                  PxCLB  (Command List Base Address)
                  PxFB   (FIS Base Address)
```

### 2.2 PCI 配置空间

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| Vendor ID | 0x00 | Intel = 0x8086 |
| Device ID | 0x02 | 具体芯片型号 |
| Class Code | 0x08 | 0x010601 = Mass Storage, SATA, AHCI |
| BAR[5]    | 0x24 | ABAR 物理地址（MMIO 基址） |
| Command   | 0x04 | Bit 1: Memory Space Enable, Bit 0: I/O Space Enable |

**枚举代码示例：**
```c
// 扫描 PCI 设备，找到 AHCI 控制器
// Class=0x01 (Mass Storage), Subclass=0x06 (SATA), ProgIF=0x01 (AHCI)
uint16_t vendor = pci_config_read16(bus, dev, func, 0x00);
uint32_t class  = pci_config_read32(bus, dev, func, 0x08);
if ((class >> 8) == 0x010601) {
    // 找到 AHCI 设备
    uint32_t abar = pci_config_read32(bus, dev, func, 0x24);
    set_pci_command(bus, dev, func, 0x04, 0x02);  // enable MMIO
}
```

### 2.3 HBA 全局寄存器

| 寄存器 | 偏移 | 大小 | 说明 |
|--------|------|------|------|
| CAP     | 0x00 | 4 bytes | HBA Capabilities |
| GHC     | 0x04 | 4 bytes | Global HBA Control |
| IS      | 0x08 | 4 bytes | Interrupt Status |
| PI      | 0x0C | 4 bytes | Ports Implemented (bitmap) |
| VS      | 0x10 | 4 bytes | Version |
| CAP2    | 0x24 | 4 bytes | Extended Capabilities |

**CAP 关键字段：**

```
Bit  0-4:  NCS   (Number of Command Slots) → Port 支持的槽位数 = NCS+1
Bit    13:  S64A  (64-bit Addressing) → 1=支持 64 位地址
Bit    18:  SNCQ  (NCQ Support) → 1=支持 NCQ
Bit    20:  SSNTF (SNTF Support) → 1=支持 SNotification
```

**GHC 关键字段：**

```
Bit  0:  HR (HBA Reset)        → 写 1 复位 HBA, 等待变为 0
Bit  1:  IE (Interrupt Enable) → 全局中断使能
Bit 31:  AE (AHCI Enable)      → 写 1 启用 AHCI
```

### 2.4 Port 寄存器（PxRegisters）

每个 Port 的寄存器基址偏移 = 0x100 + n * 0x80 (n 从 0 开始)

| 寄存器 | 偏移 | 大小 | 说明 |
|--------|------|------|------|
| PxCLB  | 0x00 | 4/8 bytes | Command List Base Address |
| PxCLBU | 0x04 | 4 bytes  | Command List Base (Upper 32-bit, 仅 S64A) |
| PxFB   | 0x08 | 4/8 bytes | FIS Base Address |
| PxFBU  | 0x0C | 4 bytes  | FIS Base (Upper 32-bit, 仅 S64A) |
| PxIS   | 0x10 | 4 bytes  | Interrupt Status |
| PxIE   | 0x14 | 4 bytes  | Interrupt Enable |
| PxCMD  | 0x18 | 4 bytes  | Command & Status |
| PxTFD  | 0x20 | 4 bytes  | Task File Data |
| PxSIG  | 0x24 | 4 bytes  | Device Signature |
| PxSSTS | 0x28 | 4 bytes  | SATA Status (Device Detection) |
| PxSERR | 0x30 | 4 bytes  | SATA Error |
| PxCI   | 0x38 | 4 bytes  | Command Issue (bitmap, 每个 slot 一位) |

**PxCMD 关键字段：**

```
Bit  0:  ST  (Start)        → 写 1 启动命令处理
Bit  4:  FRE (FIS Enable)   → 写 1 使能 FIS 接收
Bit 15:  FR  (FIS Running)   → 只读: FIS 接收 DMA 引擎正在跑
Bit 14:  CR  (Command List Running) → 只读: 命令列表 DMA 引擎正在跑
Bit 28:  MPSP (Mechanical Presence Switch Present)
```

**PxSSTS 关键字段：**

```
Bit 0-3: DET (Device Detection) → 0x0=无设备, 0x1=未初始化, 0x3=设备存在且已就绪
```

---

## 三、数据结构（内存）详解

### 3.1 Command List（每个 Port 一份）

Command List 是 1KB 大小的内存块，**1KB 对齐**，包含 32 个 Command Header。

```
偏移   大小    内容
0x00   32B    Command Header [Slot 0]
0x20   32B    Command Header [Slot 1]
...
0x3E0  32B    Command Header [Slot 31]
```

### 3.2 Command Header（每个 Slot 一个，32 bytes）

```
偏移  大小   字段
0x00  2B    DW0: CFL[4:0]   = FIS Length (DWORDS, 5=H2D Register)
             DW0: A         = ATAPI bit
             DW0: W         = Write (1=写, 0=读, 对 DMA 方向)
             DW0: P         = Prefetchable
             DW0: R         = Reset
             DW0: B         = BIST
             DW0: C         = Clear Busy upon OK
             DW0: RES       = Reserved
             DW0: PMP[3:0]  = Port Multiplier Port
             DW0: PRDTL[15:0] = PRDT Length (条目数, 每个条目 16B)
0x04  4B    DW1: PRDBC     = PRD Byte Count (传输总字节数, 完成时更新)
0x08  8B    DW2-3: CTBA    = Command Table Base Address (128-byte aligned)
0x10  16B   DW4-7: Reserved
```

**CFL 的含义：** 告诉控制器 Command Table 中 Command FIS 的大小（以 DWORD 计）。
- H2D Register FIS = 20 bytes = 5 DWORDs → CFL = 5
- 最大 16 DWORDs（64 bytes）

**PRDTL：** PRDT 的条目数。0 表示没有数据传输（如 IDENTIFY 或 SET FEATURES 等非数据命令）。

### 3.3 Command Table（每个活跃 Slot 一份）

```
偏移   大小    内容
0x00   20B    Command FIS (Host-to-Device, 5 DWORDs)
0x14   16B    ATAPI Command (不使用, 填 0)
0x24   ...    Reserved (padding to next alignment)
       n*16B  PRDT[0..N-1] (Physical Region Descriptor Table)
```

**Command Table 必须 128 字节对齐。**

### 3.4 PRDT Entry（每个条目 16 bytes）

```
偏移  大小   字段
0x00  8B    DBA  (Data Base Address) → 数据缓冲区的物理地址
0x08  4B    Reserved
0x0C  4B    DBC  (Description.B: Byte Count - 1)
             DBC[21:0] = Byte Count (0 = 4MB, 总字节数 = DBC + 1)
             DBC[31]   = I (Interrupt on Completion)
```

**DBC 的值 = 要传输的字节数 - 1。例如要传输 512 字节 → DBC = 511。**

一个 PRDT 条目最多描述 4MB 数据（DBC=0x3FFFFF）。需要传输超过 4MB 的连续数据时，可用多个 PRDT 条目描述不连续的物理页。

### 3.5 H2D Register FIS（Host-to-Device，20 bytes）

这是下发命令的核心结构：

```
偏移  大小   字段
0x00  1B    FIS Type = 0x27 (Register FIS - Host to Device)
0x01  1B    PM Port & Control
             Bit 7: C (Update Command Register)
             Bit 6: R (Update Device Register)
             Bit 5-0: PM Port (Port Multiplier)
0x02  1B    Command Register  ← 真正的 ATA 命令字节 (0xEC=IDENTIFY, 0x25=READ DMA EXT, 0x35=WRITE DMA EXT)
0x03  1B    Features (low, 7:0)
0x04  1B    LBA Low   [7:0]
0x05  1B    LBA Mid   [15:8]
0x06  1B    LBA High  [23:16]
0x07  1B    Device Register
             Bit 6: LBA mode (1 = LBA)
             Bit 5-4: DRV (Device/Head, for non-LBA)
             Bit 3-0: LBA[27:24]
0x08  1B    LBA Low   [31:24]
0x09  1B    LBA Mid   [39:32]
0x0A  1B    LBA High  [47:40]
0x0B  1B    Features (high, 15:8)
0x0C  1B    Sector Count (low, 7:0)
0x0D  1B    Sector Count (high, 15:8)
0x0E  1B    Isochronous Completion (ISO)
0x0F  1B    Control Register
0x10  4B    Auxiliary (not used, zero)
```

### 3.6 D2H Register FIS（Device-to-Host，20 bytes）

设备返回的寄存器 FIS，放在 FIS Receive Area 偏移 0x40 处：

```
偏移  大小   字段
0x00  1B    FIS Type = 0x34 (Register FIS - Device to Host)
0x01  1B    PM Port & Interrupt bit
0x02  1B    Status Register
0x03  1B    Error Register
0x04  1B    LBA Low
0x05  1B    LBA Mid
0x06  1B    LBA High
0x07  1B    Device Register
0x08  1B    LBA Low   (extended)
0x09  1B    LBA Mid   (extended)
0x0A  1B    LBA High  (extended)
0x0B  1B    Reserved
0x0C  1B    Sector Count (low)
0x0D  1B    Sector Count (high)
0x0E  2B    Reserved
0x10  4B    Reserved
```

### 3.7 Received FIS Structure（FIS Receive Area，256 bytes）

```
偏移   大小    内容
0x00   0x20   DMA Setup FIS (Device to Host)
0x20   0x18   PIO Setup FIS (Device to Host)
0x40   0x14   D2H Register FIS (Device to Host)  ← 命令返回结果在此读取
0x58   0x04   Set Device Bits FIS
0x60   0x58   Unknown FIS (catch-all)
0xB8   0x08   Reserved
0xC0   0x40   Custom / Vendor Specific
```

**FIS Receive Area 必须 256 字节对齐。**

---

## 四、初始化流程

```
Step 1: PCI 枚举
  ├── 扫描 PCI bus, 匹配 Class=0x01, Subclass=0x06, ProgIF=0x01
  ├── 读 PCI BAR[5] ← ABAR 物理地址
  ├── 映射 ABAR 到内核虚地址空间
  └── 设置 PCI Command Register: Bus Master + MMIO Enable

Step 2: HBA Reset
  ├── 写 GHC.AE = 0        (关闭 HBA)
  ├── 写 GHC.HR = 1        (触发复位)
  ├── while (GHC.HR == 1)  (等待复位完成)
  ├── 写 GHC.AE = 1        (重新启用)
  └── 读 CAP → NCS, S64A, NP

Step 3: Port 探测
  ├── 读 PI 位图
  └── 对每个活跃 Port:
       ├── 检查 PxSSTS.DET == 0x3 (设备就绪)
       └── 进行 Port Reset Sequence

Step 4: Port Reset
  ├── 写 PxCMD.ST  = 0     (停止命令处理)
  ├── while (PxCMD.CR == 1)(等待命令引擎停)
  ├── 写 PxCMD.FRE = 0     (关闭 FIS 接收)
  ├── while (PxCMD.FR == 1)(等待 FIS 停)
  ├── 写 PxCMD.FRE = 1     (使能 FIS 接收)
  ├── while (PxCMD.FR == 0)(等待 FIS 就绪)
  ├── 写 PxCMD.ST  = 1     (启动)
  └── while (PxCMD.CR == 0)(等待命令引擎就绪)

Step 5: 分配内存
  ├── Command List:     1KB, 1KB 对齐  → 写入 PxCLB
  ├── Command Tables:   每个 Slot 128B 对齐
  ├── FIS Receive Area: 256B, 256B 对齐 → 写入 PxFB
  └── 所有地址用物理地址

Step 6: 配置中断
  ├── 写 PxIE = 掩码 (使能 Port 中断)
  └── 写 GHC.IE = 1  (全局中断使能)

Step 7: 发起 IDENTIFY
  ├── 构造 H2D Register FIS (Command=0xEC)
  ├── 填充 Command Header (CFL=5, PRDTL=1, W=0)
  ├── 填充 PRDT[0] (DBA=缓冲区, DBC=511)
  ├── 写 PxCI |= (1 << slot)    (发令)
  ├── while (PxCI & (1 << slot)) (轮询完成)
  └── 解析 IDENTIFY 返回数据
```

---

## 五、常见 ATA 命令

| 命令 | 代码 | 说明 | DMA | LBA |
|------|------|------|-----|-----|
| IDENTIFY DEVICE | 0xEC | 返回设备信息（型号/容量） | 读 PIO | N/A |
| READ DMA EXT | 0x25 | LBA48 读扇区 | 读 | LBA48 |
| WRITE DMA EXT | 0x35 | LBA48 写扇区 | 写 | LBA48 |
| READ SECTORS EXT | 0x24 | LBA48 读扇区 (PIO) | 否 | LBA48 |
| WRITE SECTORS EXT | 0x34 | LBA48 写扇区 (PIO) | 否 | LBA48 |
| FLUSH CACHE EXT | 0xEA | 刷缓存到磁盘 | 否 | N/A |

**注意：** 对于 AHCI 新手，建议先从 **READ/WRITE DMA EXT** 开始（通过 PRDT 描述缓冲区），不要用 NON-DMA 命令。原因是 AHCI 的 FIS-Based PIO 路径比传统的 IDE PIO 复杂，而且 DMA 才是 AHCI 设计的本意。

---

## 六、代码骨架：读取一个扇区

```c
// 伪代码——关键的发送命令流程
int ahci_read_sector(ahci_port_t *port, uint64_t lba, void *buf, uint32_t count) {
    int slot = ahci_find_free_slot(port);
    if (slot < 0) return -1;

    // 1. 填充 Command Table
    cmd_table_t *table = port->cmd_tables[slot];
    memset(table, 0, sizeof(cmd_table_t));

    // 构造 H2D Register FIS
    fis_h2d_t *fis = &table->cmd_fis;
    fis->fis_type = 0x27;           // H2D Register FIS
    fis->ctrl     = 0x80;           // C bit: Update Command
    fis->command  = 0x25;           // READ DMA EXT
    fis->device   = 0x40;           // LBA mode
    fis->lba0     = (lba >>  0) & 0xFF;
    fis->lba1     = (lba >>  8) & 0xFF;
    fis->lba2     = (lba >> 16) & 0xFF;
    fis->lba3     = (lba >> 24) & 0xFF;
    fis->lba4     = (lba >> 32) & 0xFF;
    fis->lba5     = (lba >> 40) & 0xFF;
    fis->sect_cnt0 = (count >> 0) & 0xFF;
    fis->sect_cnt1 = (count >> 8) & 0xFF;

    // 填充 PRDT
    table->prdt[0].dba  = virt_to_phys(buf);
    table->prdt[0].dbc  = (count * 512) - 1;  // DBC = byte count - 1

    // 2. 填充 Command Header
    volatile cmd_header_t *header = &port->cmd_list[slot];
    memset((void *)header, 0, sizeof(cmd_header_t));
    header->cfl    = 5;            // 5 DWORDs (20 bytes)
    header->w      = 0;            // Read (DMA from device to memory)
    header->prdtl  = 1;            // 1 PRDT entry
    header->ctba   = virt_to_phys(table);

    // 3. 通知控制器
    port->regs->pxci |= (1 << slot);

    // 4. 轮询等待完成
    while (port->regs->pxci & (1 << slot)) {
        // 超时处理
    }

    // 5. 检查结果
    fis_d2h_t *resp = (fis_d2h_t *)&port->fis_area->d2h_fis;
    if (resp->status & 0x01) {  // ERR bit
        return -1;
    }

    return 0;
}
```

---

## 七、ATAPI 命令（CD/DVD 光驱）

如果你的老笔记本有光驱并且按的是 AHCI Port，设备签名（PxSIG）会是 `0xEB140101`。

ATAPI = SCSI 命令封装在 ATA 封包内。发 PACKET 命令（0xA0）来传输：
- Command FIS 的 Command = 0xA0（PACKET）
- ATAPI Command 放到 Command Table 的 ACmd 字段（12-16 bytes）
- PRDT 描述数据缓冲区
- 读取扇区的 SCSI 命令 = `READ(10)` = `0x28 00 [LBA 4B] 00 [Sector Count 2B] 00 00 00`

对于本项目的 OS，光驱驱动优先级较低，可以把光驱口标记为 "no medium" 跳过。

---

## 八、开发与调试建议

### 8.1 QEMU 先验证

```bash
# 创建一个 64MB 磁盘镜像
dd if=/dev/zero of=disk.img bs=1M count=64

# 启动 QEMU 带 AHCI 控制器
qemu-system-i386 \
    -device ahci,id=ahci \
    -drive file=disk.img,if=none,id=disk,format=raw \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -kernel your_kernel.elf
```

QEMU 的 AHCI 模拟是寄存器级精确的，如果驱动逻辑正确，在 QEMU 上跑通后再上实机基本无缝。

### 8.2 调试时盯紧的寄存器

| 寄存器 | 现象 | 含义 |
|--------|------|------|
| PxCMD | CR=0, FR=0 | Port 空闲，可以操作 |
| PxCI  | Bit 不清零 | 命令卡住了，控制器不在运行 |
| PxTFD.ERR | 非零 | 设备报错（SATA 层） |
| PxSERR | 非零 | SATA 链路错误 |
| PxIS  | 中断位 | 发生了什么类型的中断 |
| PxSSTS.DET | 0x0 | 没有设备（口子空的） |
| PxSSTS.DET | 0x3 | 设备就绪 |

### 8.3 常见坑

1. **内存对齐**
   - Command List → 1KB 对齐
   - FIS Receive Area → 256B 对齐
   - Command Table → 128B 对齐
   - 不满足对齐硬件直接报 PRCRC（CRC Error）

2. **字节序**
   - AHCI 寄存器全是 Little-Endian（x86 原生，无需转换）
   - IDENTIFY 返回的字数组也是小端的

3. **等待标志位**
   - `GHC.HR` → 写 1 后等它变成 0
   - `PxCMD.CR` → 写 ST=0 后等它变成 0，写 ST=1 后等它变成 1
   - `PxCMD.FR` → 同理
   - 各种启动停止都不能跳过等待步骤

4. **PRDT DBC 可不是字节数**
   - `DBC = 实际的字节数 - 1`
   - DBC = 0 表示 1 byte
   - DBC = 511 表示 512 bytes

5. **IDENTIFY 的 PRDT**
   - IDENTIFY 命令需要一块 512 字节的数据缓冲区 (容纳 ATA IDENTIFY data 结构)
   - PRDT[0].DBC = 511

6. **读和写的方向**
   - 读扇区 (0x25)：数据从设备 DMA 到主机
   - 写扇区 (0x35)：数据从主机 DMA 到设备
   - Command Header 的 W bit 用来通知控制器 DMA 方向（尽管 ATA 命令本身就已经定义了方向，但 AHCI 还是要设）

---

## 九、开发路线图

| 阶段 | 内容 | 码量估计 | 里程碑 |
|------|------|----------|--------|
| **Phase 1** | PCI 枚举 + ABAR 映射 | ~100 行 | "Found AHCI at 00:17.0" |
| **Phase 2** | HBA + Port 初始化 | ~150 行 | "Port 0 ready, device present" |
| **Phase 3** | IDENTIFY 命令 | ~200 行 | 打印硬盘型号和容量 |
| **Phase 4** | READ DMA EXT（轮询） | ~100 行 | 读取扇区数据 |
| **Phase 5** | WRITE DMA EXT（轮询） | ~60 行 | 写入扇区数据 |
| **Phase 6** | 中断驱动 + 异步 I/O | ~150 行 | 后台 I/O 不占 CPU |
| **Phase 7** | NCQ（Native Command Queue）| ~200 行 | 吞吐大幅提升 |
| **总计** | | ~900 行 | |

本项目起始阶段需要的只是 Phase 1-4（约 550 行代码），即可支持完整的磁盘读写。

---

## 十、参考资料

- [AHCI Specification v1.3.1](https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html) — 核心规范
- [ATA/ATAPI Command Set (ACS-4)](https://www.t13.org/) — ATA 命令参考
- [OSDev Wiki - AHCI](https://wiki.osdev.org/AHCI) — OS 开发者视角的简化指南
- [Intel 7 Series / 8 Series PCH Datasheet](https://www.intel.com/) — PCH 内 AHCI 控制器细节（你的 PCH 属于 Sunrise Point-LP）
- QEMU 源码 `hw/ide/ahci.c` — 参考实现，寄存器行为最精确
