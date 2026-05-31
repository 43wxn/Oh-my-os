# 系统构建与实机部署

> Oh-my-os 面向实机运行的操作系统。
> 构建产物为标准磁盘镜像，支持 U 盘启动和硬盘安装。

---

## 一、构建流水线

```
源码 (boot/ + kernel/ + user/)
  │
  │  make
  ▼
build/
  ├── boot_mbr.bin        MBR 引导扇区 (512B)
  ├── boot_vbr.bin        FAT32 卷引导记录 (512B)
  ├── kernel.elf          内核 ELF (带符号表)
  ├── kernel.bin          内核扁平二进制
  ├── user/*.elf          用户态程序
  │
  │  make image
  ▼
build/disk.img
  │  标准 FAT32 分区磁盘镜像 (~128MB)
  │
  │  dd → U 盘 (开发测试)
  │  dd → 硬盘分区 (正式部署)
  ▼
ASUS X542UF 实机启动
```

**交叉编译工具链：**

```bash
sudo apt install nasm i686-elf-gcc i686-elf-binutils
```

目标三元组: `i686-elf`，生成 32 位保护模式代码，不链接宿主 libc。

---

## 二、磁盘布局（FAT32 分区方案）

一个面向实机的操作系统必须使用标准分区表，这样宿主机能 mount、GRUB 能 chainload、数据能持久化。裸扇区方案是不可接受的。

```
┌──────────────────────────────────────────────────────────┐
│                   disk.img (128 MB)                      │
├──────────────────────────────────────────────────────────┤
│ Sector 0: MBR                                            │
│  ├── 0x000-0x1BD  Boot code (加载 VBR 并跳转)              │
│  └── 0x1BE-0x1FD  Partition table                        │
│       Entry 0: Type=0x0C (FAT32 LBA), Start=2048, ...    │
├──────────────────────────────────────────────────────────┤
│ Sector 2048: VBR (FAT32 Volume Boot Record)              │
│  ├── BPB (BIOS Parameter Block) — 文件系统元数据            │
│  ├── Boot code — 读 FAT 表，找 /boot/kernel.elf，加载       │
│  └── 0x55AA                                              │
├──────────────────────────────────────────────────────────┤
│ Reserved sectors (32 total from VBR)                     │
├──────────────────────────────────────────────────────────┤
│ FAT #1, FAT #2                                           │
├──────────────────────────────────────────────────────────┤
│ Data clusters:                                           │
│  ├── /boot/                                              │
│  │   └── kernel.elf      内核镜像                        │
│  ├── /bin/                                               │
│  │   ├── shell           命令行解释器                    │
│  │   ├── ls              文件列表                        │
│  │   ├── cat             文件查看                        │
│  │   ├── echo            文本输出                        │
│  │   ├── ps              进程列表                        │
│  │   ├── kill            进程终止                        │
│  │   └── init            PID 1 初始化进程                │
│  ├── /etc/                                               │
│  │   └── motd            登录欢迎语                      │
│  └── /home/                                              │
│      └── (用户目录)                                       │
└──────────────────────────────────────────────────────────┘
```

**为什么不裸扇区？**
- 裸扇区没有分区表，宿主机 Linux 无法 mount，每次更新文件都要重 dd 整个镜像
- FAT32 分区可以在开发机上 mount，直接 `cp` 新文件进去
- 标准 MBR 分区表意味着 GRUB 可以 chainload，支持双启动

---

## 三、分区表参数

```c
// MBR 分区表 Entry 0
typedef struct {
    uint8_t  status;       // 0x80 = bootable
    uint8_t  chs_start[3]; // 起始 CHS
    uint8_t  type;         // 0x0C = FAT32 LBA
    uint8_t  chs_end[3];   // 结束 CHS
    uint32_t lba_start;    // 2048 (1MB 对齐，给 MBR + reserved 留空间)
    uint32_t lba_count;    // 总扇区数
} __attribute__((packed)) partition_entry_t;
```

**VBR BPB 关键字段（FAT32）：**

| 字段 | 值 | 说明 |
|------|-----|------|
| BytesPerSector | 512 | |
| SectorsPerCluster | 8 | 4KB/cluster |
| ReservedSectors | 32 | 含 VBR 自身 |
| NumFATs | 2 | |
| SectorsPerFAT | 计算 | 取决于分区大小 |
| RootCluster | 2 | FAT32 根目录起始簇号 |
| BootSignature | 0x29 | |
| VolumeLabel | "OHMYOS" | |

---

## 四、Boot 流程

```
BIOS POST
  │
  └── 读 MBR (Sector 0) → 0x7C00
        │
        └── MBR 扫描分区表, 找 Active (0x80) 分区
              │
              └── 读该分区的 VBR → 0x7C00
                    │
                    └── VBR BPB 提供文件系统参数
                     VBR boot code:
                       1. 加载 FAT 表到内存
                       2. 从根目录开始遍历目录树
                       3. 找到 /boot/kernel.elf
                       4. 解析 ELF header
                       5. 加载各 segment 到内存
                       6. 切换到保护模式
                       7. 跳转到 kernel entry point
```

**与 GRUB 的兼容性：** 因为磁盘有标准 MBR 分区表和 FAT32 分区，GRUB 能直接识别。如果用户装了双系统，可以在 GRUB 里加一条：

```
menuentry "Oh-my-os" {
    set root=(hd0,msdos1)
    chainloader +1
}
```

这样用户开机先进 GRUB，选择 Oh-my-os 再 chainload 到我们的 VBR。

---

## 五、从源码到可启动 U 盘

```bash
# 1. 编译所有源码
make all
#   → build/boot_mbr.bin
#   → build/boot_vbr.bin
#   → build/kernel.elf
#   → build/user/*.elf

# 2. 生成磁盘镜像
make image
#   → build/disk.img (128MB, FAT32)

# 3. 写入 U 盘
make install USB=/dev/sdb
#   dd if=build/disk.img of=/dev/sdb bs=4M status=progress

# 4. 插入笔记本，设置 BIOS 从 USB-HDD 启动
```

---

## 六、安装到笔记本硬盘

### 6.1 当前硬盘布局

```
NAME   SIZE   MOUNTPOINTS
sda    111.8G
├─sda1   1.5G  /boot          ← Deepin 的 boot 分区
├─sda2    1K                   ← 扩展分区标记
├─sda3    11G  /recovery      ← Deepin 恢复分区
├─sda4    11G  [SWAP]         ← Linux Swap
├─sda5    15G  /              ← Deepin 根分区
├─sda6    15G  (Deepin 备用)
└─sda7  58.3G  /var /home ... ← 数据分区
```

### 6.2 双启动方案

从 sda7 的 58GB 中分 8GB 给 Oh-my-os（建议 4-8GB 足够）：

```bash
# 在 Deepin 中操作（笔记本上）
sudo parted /dev/sda
  (parted) resizepart 7 50G         # sda7 缩小到 50G
  (parted) mkpart primary fat32 50G 58G  # 新建 sda8
  (parted) set 8 boot on
  (parted) quit

# 在开发机上烧录
sudo dd if=build/disk.img of=/dev/sda8 bs=4M status=progress

# 回到笔记本，更新 GRUB
sudo update-grub   # os-prober 会自动发现 Oh-my-os
```

### 6.3 全盘替换方案

如果最终不需要 Deepin：

```bash
# 从 U 盘启动开发机上的 Live Linux
# 然后整盘 dd
sudo dd if=/path/to/disk.img of=/dev/sda bs=4M status=progress
```

---

## 七、首次硬件验证

在开始写内核 C 代码之前，先用一段最小汇编验证"能控制真机"：

**目标：** 512 字节 MBR → U 盘 → 笔记本开机 → VGA 输出 `OK` → 证明链路通。

```asm
; verify.asm — 硬件链路验证
[bits 16]
[org 0x7C00]

    ; 设置 VGA 文本模式 80x25
    mov ax, 0x0003
    int 0x10

    ; 直接写显存 (0xB8000)
    mov ax, 0xB800
    mov es, ax

    mov byte [es:0],  'O'
    mov byte [es:1],  0x0F
    mov byte [es:2],  'K'
    mov byte [es:3],  0x0F

    cli
    hlt

times 510-($-$$) db 0
dw 0xAA55
```

```bash
nasm -f bin verify.asm -o verify.bin
sudo dd if=verify.bin of=/dev/sdb bs=512 count=1
sync
```

这一步的意义不是"写玩具"，而是确认：**你的工具链、U 盘烧录流程、笔记本 BIOS 设置、VGA 硬件——整条链路是通的。**

---

## 八、Makefile 集成

```makefile
DISK_IMG = build/disk.img
DISK_SIZE = 128

# 生成磁盘镜像
.PHONY: image
image: all tools/mkfs
	@echo "[IMAGE] Creating $(DISK_IMG) ($(DISK_SIZE)MB)..."
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE) 2>/dev/null
	@# 分区表 + MBR
	@dd if=build/boot_mbr.bin of=$(DISK_IMG) bs=446 count=1 conv=notrunc 2>/dev/null
	@# 创建 FAT32 并写入 VBR
	./tools/mkfs $(DISK_IMG) $(DISK_SIZE) build/boot_vbr.bin
	@# Mount & populate
	@mkdir -p /tmp/ohmyos_mnt
	@sudo mount $(DISK_IMG) /tmp/ohmyos_mnt
	@sudo mkdir -p /tmp/ohmyos_mnt/boot /tmp/ohmyos_mnt/bin /tmp/ohmyos_mnt/etc
	@sudo cp build/kernel.elf /tmp/ohmyos_mnt/boot/
	@sudo cp build/user/*.elf /tmp/ohmyos_mnt/bin/
	@echo "Welcome to Oh-my-os" | sudo tee /tmp/ohmyos_mnt/etc/motd > /dev/null
	@sudo umount /tmp/ohmyos_mnt
	@echo "[IMAGE] Done: $(DISK_IMG)"

# 写入目标设备
.PHONY: install
install: image
ifndef USB
	$(error Usage: make install USB=/dev/sdX)
endif
	@echo "[INSTALL] Writing to $(USB)..."
	sudo dd if=$(DISK_IMG) of=$(USB) bs=4M status=progress conv=fsync
	@echo "[INSTALL] Complete."
```

---

## 九、与真正发行版的差距

这个系统离"日常可用的 OS"还差什么——诚实列出来，后续迭代：

| 能力 | 状态 | 说明 |
|------|------|------|
| 内核 | 待实现 | 内存管理/进程调度/系统调用 |
| 磁盘驱动 | 待实现 | AHCI 读写 |
| 文件系统 | 待实现 | FAT32 读/写/创建/删除 |
| Shell | 待实现 | 基本命令执行 |
| **网络栈** | **缺失** | TCP/IP 协议栈 + 网卡驱动 (RTL8111) |
| **USB 栈** | **缺失** | XHCI 控制器驱动 (有 USB 3.0 控制器) |
| **图形界面** | **缺失** | GUI/window manager |
| **多核** | **缺失** | SMP 调度 |
| **ACPI** | **缺失** | 电源管理 / 关机 / 休眠 |

短期目标：前 5 项全部实现 → 得到一个可以通过命令行交互的操作系统。
中期目标：加入网络栈 → 能用 `curl`-like 工具访问 HTTP。
长期目标：图形界面 + USB 外设支持。
