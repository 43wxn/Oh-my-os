# M3 阶段文档: 内存管理

> 日期: 2026-05-31
> 状态: ✅ 完成
> 实机验证: ASUS X542UF 通过 (E820 内存探测 + PMM + VMM + Heap + Demand Paging)

## 一、阶段目标

实现完整的内存管理子系统：BIOS E820 物理内存探测 → 位图物理页框分配器 → 二级页表虚拟内存 → kmalloc/kfree 内核堆 → #PF 按需分页。

## 二、实机输出

```
+------------------------------------------+
|  Oh-my-os Kernel v0.0.3 (M3)             |
+------------------------------------------+

[1] IDT...
[1] IDT OK
[2] PIC...
[2] PIC OK
[3a] E820 entries at 0x2000: 6
     [0] base=0x0 len=0x9FC00 type=1
     [1] base=0x9FC00 len=0x400 type=2
     [2] base=0xF0000 len=0x10000 type=2
     [3] base=0x100000 len=0x7EE0000 type=1
     [4] base=0x7FE0000 len=0x20000 type=2
     [5] base=0x8000000 len=0x78000000 type=1
[3b] PMM init...
[PMM] E820 buffer at 0x2000, count=6
[PMM] Max phys addr: 0x80000000, total_pages=524288 (2048 MB)
[PMM] Bitmap: 16 pages (64 KB) at 0x10A000
[PMM]   [0] 0x0 - 0x9FC00  type=1  (0 MB)
[PMM]   [3] 0x100000 - 0x7FE0000  type=1  (126 MB)
[PMM]   [5] 0x8000000 - 0x80000000  type=1  (1920 MB)
[PMM] Reserved: kernel+bitmap 298 pages, VGA/BIOS 0xA0-0xFF
[PMM] Free pages: 563808 (2202 MB)
[3b] PMM OK
[3c] VMM init...
[VMM] Page directory at phys 0x11A000
[VMM] Page table 0 (0-4MB) identity-mapped
[VMM] Paging enabled (CR0.PG=1)
[3c] VMM OK, paging=1
[3d] Heap init...
[HEAP] Heap range: 0xD0000000 - 0xE0000000
[3d] Heap OK

----------------------------------------
Press any key to start the kernel...
[KBD] PS/2 keyboard initialized (IRQ1)
[3] PIT...
[3] Running. jiffies=0

[T] 100
[T] 200
```

## 三、架构

```
                    ┌────────────────────────┐
                    │   BIOS INT 0x15 E820   │ 实模式, stage2 调用
                    │   写入 0x2000 缓冲区     │
                    └───────────┬────────────┘
                                │
              ┌─────────────────┴─────────────────┐
              ▼                                   ▼
    ┌──────────────────┐              ┌──────────────────┐
    │     PMM          │              │     VMM          │
    │  物理页框分配器    │◄──────────── │  虚拟内存管理器    │
    │                   │  pmm_alloc   │                   │
    │  位图法           │  pmm_free    │  二级页表          │
    │  alloc/free       │              │  PDE → PTE → 页   │
    └────────┬──────────┘              │  map/unmap        │
             │                         │  identity 0-4MB   │
             │ 物理页                  └────────┬──────────┘
             ▼                                  │
    ┌──────────────────┐                        │ 虚拟地址
    │     Heap         │◄───────────────────────┘
    │  内核堆分配器      │
    │                   │
    │  整页分配         │
    │  kmalloc/kfree    │
    │  krealloc         │
    │  0xD0000000+      │
    └────────┬──────────┘
             │
             ▼
    ┌──────────────────┐
    │  Demand Paging   │  #PF handler (INT 0x0E)
    │  按需分页         │
    │                   │
    │  #PF → alloc_page │
    │  → map_page      │
    │  → iret 重试      │
    └──────────────────┘
```

## 四、文件清单

| 文件 | 行数 | 职责 |
|------|------|------|
| `boot/stage2.S` | 512 | 实模式 E820 探测 (INT 0x15), 写入 0x2000 |
| `kernel/mm/pmm.h` | 36 | E820 结构体 (24 bytes packed), PMM API |
| `kernel/mm/pmm.cc` | 150 | 位图物理页框分配器, 两次遍历 E820 |
| `kernel/mm/vmm.h` | 29 | 虚拟内存管理器 API |
| `kernel/mm/vmm.cc` | 162 | 二级页表, identity-map 0-4MB, CR3 加载 |
| `kernel/mm/heap.h` | 22 | 堆分配器 API |
| `kernel/mm/heap.cc` | 133 | 整页分配器, kmalloc/kfree/krealloc |
| `kernel/arch/x86/paging.h` | 74 | CR0/CR3 操作, TLB 刷新, 页表常量 |
| `kernel/arch/x86/pic.cc` | 68 | PIC 初始化 (无 io_wait) |
| `kernel/arch/x86/isr.cc` | 120 | #PF demand paging + 异常停机 |
| `kernel/kernel.cc` | 82 | 顺序初始化 + E820 调试输出 |

## 五、关键技术实现

### 5.1 E820 物理内存探测

Stage2 在实模式下循环调用 BIOS INT 0x15 (AX=0xE820):

```asm
; E820 数据布局 @ 0x2000:
;   [count: dword] [entry0: 24 bytes] [entry1: 24 bytes] ...

detect_e820:
    movw    $E820_SEG, %ax       ; ES = 0x0200 → 物理 0x2000
    movw    %ax, %es
    addw    $4, %di              ; 跳过 count 字段
    xorl    %ebx, %ebx           ; continuation = 0
1:
    movl    $0xE820, %eax
    movl    $24, %ecx            ; 请求 ACPI 3.0 24 字节
    movl    $0x534D4150, %edx    ; 'SMAP'
    int     $0x15
    jc      2f
    cmpl    $0x534D4150, %eax
    jne     2f
    incw    %es:0x0000           ; count++
    addw    $24, %di             ; 固定 24 字节步进
    testl   %ebx, %ebx
    jnz     1b
2:
    ret
```

PMM 两次遍历 E820 数据:
- **Pass 1**: 找最大物理地址 → 确定位图覆盖范围
- **Pass 2**: 只标记 `type=1` (Usable) 区域为空闲

E820 无效时自动回退 Safe-Zone (1MB-128MB)。

### 5.2 物理页框分配器 (PMM)

位图法: 每 bit = 1 个 4KB 页。

```
位图布局:
  bitmap[0] bit 0 → 物理页 0 (0x0000-0x0FFF)
  bitmap[0] bit 1 → 物理页 1 (0x1000-0x1FFF)
  ...
  bitmap[n] bit m → 物理页 (n*8 + m)

0 = 空闲, 1 = 已分配
```

API:
```c
void*  pmm_alloc_page();              // 线性扫描, 返回物理地址
void   pmm_free_page(void *phys);     // 释放单页
uint32_t pmm_total_pages();           // 总页数
uint32_t pmm_free_pages();            // 空闲页数
```

分配器扫描从页 1 开始 (保留页 0 避免 NULL 歧义)。

### 5.3 二级页表 (VMM)

x86 32-bit 分页: PDE (10 bits) → PTE (10 bits) → Offset (12 bits)

```
虚拟地址: | PDE_idx (10) | PTE_idx (10) | Offset (12) |
             ↓               ↓
           CR3 → PD        PT[1024]      物理页
              PDE[1024]    每项 4B
              每项 4B
```

初始化流程:
1. `pmm_alloc_page()` → 页目录 (4KB 对齐)
2. `pmm_alloc_page()` → 页表 0
3. PT0[i] = (i * 4096) | PRESENT | RW  (identity map 0-4MB)
4. PD[0] = PT0 | PRESENT | RW
5. `movl %cr3, pd_phys` → 加载 CR3
6. `movl %cr0, %eax; orl $0x80000000; movl %eax, %cr0` → 启用分页

identity-map 0-4MB 确保内核栈、VGA 显存、内核代码在分页启用后仍然可访问。

API:
```c
int  vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);
uint32_t vmm_unmap_page(uint32_t vaddr);
uint32_t vmm_virt_to_phys(uint32_t vaddr);
```

map_page 在 PDE 不存在时自动分配新页表。

### 5.4 内核堆 (kmalloc/kfree)

整页分配器, 堆区域: `0xD0000000 - 0xE0000000` (256MB 内核虚拟地址空间)。

```
kmalloc(size):
  1. total = size + sizeof(block_hdr)     ; 加上块头
  2. pages = ceil(total / 4096)           ; 向上取整到页
  3. for each page:
       phys = pmm_alloc_page()            ; 分配物理页
       vmm_map_page(vaddr, phys, RW)     ; 映射到堆区
  4. 写入 block_hdr (magic=0x4B4D414C, size)
  5. 返回 &hdr[1] (用户指针)

kfree(ptr):
  1. hdr = ptr - sizeof(block_hdr)        ; 回推块头
  2. 验证 magic
  3. for each page:
       phys = vmm_unmap_page(vaddr)
       pmm_free_page(phys)
```

当前限制: 堆只增长不收缩, 虚拟地址不回收 (简化实现)。

### 5.5 Demand Paging (#PF 处理器)

`isr_handler` 中处理 INT 0x0E (Page Fault):

```
#PF 处理流程:
  1. 读 CR2 → 故障虚拟地址
  2. 检查 error_code bit 0:
     bit0=0 → 页不存在 (可修复)
     bit0=1 → 保护违例 (不可修复, 停机)
  3. vaddr & 0xFFFFF000 → 页对齐
  4. 验证 vaddr >= 0x1000 (拒绝 NULL 页)
  5. phys = pmm_alloc_page()
  6. flags = PRESENT | RW [+ USER (if error bit2)]
  7. vmm_map_page(vaddr, phys, flags)
  8. return → CPU 重试触发 #PF 的指令
```

error_code 位定义:
- bit 0 (P): 0=页不存在, 1=保护违例
- bit 1 (W/R): 0=读, 1=写
- bit 2 (U/S): 0=内核态, 1=用户态

## 六、实机踩坑

| 现象 | 根因 | 修复 |
|------|------|------|
| pic_init 后立即异常 | `io_wait()` 写 port 0x80, 华硕 EC 不兼容 | 移除所有 `io_wait()` 调用 |
| E820 数据全乱码 | E820 buffer 0x9000 与内核加载地址 0x8000 重叠 (内核 >4KB 时覆盖 isr_handler) | 搬至 0x2000 |
| printk 输出 `%08x` 字样 + 参数错位 | printk 只支持 `%d/%x/%s/%c`, 不认 `%llx/%08x` | 64 位值拆为两段 `%x%x`, 去掉所有修饰符 |
| QEMU 正常实机崩溃 | 内核 32.2 扇区但 stage2 只读 32 扇区, 最后 148B 截断 | KERNEL_SECTORS 增至 64 |
| Shift 不生效 | i8042 翻译后是 Set 1 码 (Shift=0x2A/0x36), 代码检测 Set 2 码 (0x12/0x59) | 改用 Set 1 扫描码, bit7 检测断码 |
| tick 信息滚屏淹没 M3 诊断 | 初始化完立即进入 tick 循环 | 加 "Press any key" 暂停 |
| Stage2 E820 count 显示 `0F0F` | `print_hex8_rm` 低 4 位 bug: `movb $0x0F, %bl` 覆盖了原始值 | 改用 CL 保存原始值 |

## 七、内存布局

```
物理地址空间 (实机 ASUS X542UF, 8GB → 32bit 管理 4GB):
  0x00000000 - 0x0009FBFF : 可用 (640KB 常规内存)
  0x0009FC00 - 0x0009FFFF : ACPI 保留
  0x000A0000 - 0x000FFFFF : VGA 显存 + BIOS ROM
  0x00100000 - 0x07FDFFFF : 可用 (~127MB)
  0x07FE0000 - 0x07FFFFFF : 保留
  0x08000000 - 0x7FFFFFFF : 可用 (~1.9GB)
  0x80000000+            : >4GB, 32bit 无 PAE 不可达

内核占用:
  0x00100000 - 0x00106531 : 内核代码 + 数据 (16532 bytes)
  0x00105000 - 0x00106000 : BSS (未初始化数据)
  0x00106000 - 0x0010A000 : 内核栈 (16KB)
  0x0010A000+            : PMM 位图 (跟随内核增长)

虚拟地址空间:
  0x00000000 - 0x003FFFFF : identity-map (0-4MB)
  0xD0000000 - 0xE0000000 : 内核堆 (kmalloc)
```

## 八、下一步 (M4)

进程管理: PCB 数据结构 → 上下文切换 (switch_to) → 调度器 (Round Robin) → fork/exec/exit/wait 系统调用 → 同步原语 (信号量/互斥锁)。
