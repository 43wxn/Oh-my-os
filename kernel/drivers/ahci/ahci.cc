/* ============================================================================
 * kernel/drivers/ahci/ahci.cc — AHCI SATA 驱动实现
 *
 * 轮询模式 (无中断), 单 command slot。
 * 支持 LBA48 扇区读写。
 *
 * 参考: Serial ATA AHCI 1.3 Specification
 * ============================================================================ */

#include "kernel/drivers/ahci/ahci.h"
#include "kernel/drivers/pci/pci.h"
#include "kernel/arch/x86/port.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/mm/heap.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/printk.h"
#include "kernel/lib/serial.h"

/* ── 全局状态 ── */
static volatile uint32_t *hba_base = nullptr;  /* HBA MMIO 虚拟地址基址       */
static uint32_t            hba_phys  = 0;       /* HBA MMIO 物理地址基址       */
static int                 hba_port_count = 0;   /* 可用端口数                  */
static int                 active_port = -1;     /* 当前使用的端口索引          */
static uint64_t            disk_sectors = 0;     /* 磁盘总扇区数                */

/* Port 0 的内存结构 (物理连续) */
static ahci_cmd_header  *port_cl   = nullptr;    /* Command List (物理地址)     */
static ahci_recv_fis    *port_fis  = nullptr;    /* Received FIS (物理地址)     */
static ahci_cmd_table   *port_ct   = nullptr;    /* Command Table (物理地址)    */
static uint32_t port_cl_phys  = 0;
static uint32_t port_fis_phys = 0;
static uint32_t port_ct_phys  = 0;

/* ── 辅助: MMIO 读写 ── */
static inline uint32_t hba_read(uint32_t offset) {
    return *(volatile uint32_t *)((uint8_t *)hba_base + offset);
}

static inline void hba_write(uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)((uint8_t *)hba_base + offset) = val;
}

static inline uint32_t port_read(int port, uint32_t offset) {
    return *(volatile uint32_t *)((uint8_t *)hba_base + 0x100 + port * 0x80 + offset);
}

static inline void port_write(int port, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)((uint8_t *)hba_base + 0x100 + port * 0x80 + offset) = val;
}

/* ── HBA 复位 ── */
static int hba_reset() {
    uint32_t ghc = hba_read(0x04);  /* GHC */
    if (!(ghc & HBA_GHC_AE)) {
        /* AHCI 未使能, 先使能 */
        hba_write(0x04, ghc | HBA_GHC_AE);
    }

    /* 断言 HR (HBA Reset) */
    hba_write(0x04, hba_read(0x04) | HBA_GHC_HR);

    /* 等待复位完成 (最多 1000ms) */
    for (int i = 0; i < 1000; i++) {
        if (!(hba_read(0x04) & HBA_GHC_HR)) {
            printk("[AHCI] HBA reset OK\n");
            return 0;
        }
        /* 等约 1ms */
        for (volatile int j = 0; j < 100000; j++) {
            __asm__ volatile ("pause");
        }
    }
    printk("[AHCI] ERROR: HBA reset timeout!\n");
    return -1;
}

/* ── 端口停止 ── */
static void port_stop(int port) {
    uint32_t cmd = port_read(port, PORT_CMD);

    /* 清 ST (Start) */
    cmd &= ~PORT_CMD_ST;
    port_write(port, PORT_CMD, cmd);

    /* 等待 CR (Command List Running) 清零 */
    for (int i = 0; i < 500; i++) {
        if (!(port_read(port, PORT_CMD) & PORT_CMD_CR)) {
            break;
        }
        for (volatile int j = 0; j < 10000; j++) {
            __asm__ volatile ("pause");
        }
    }

    /* 清 FRE (FIS Receive Enable) */
    cmd = port_read(port, PORT_CMD);
    cmd &= ~PORT_CMD_FRE;
    port_write(port, PORT_CMD, cmd);

    /* 等待 FR (FIS Receive Running) 清零 */
    for (int i = 0; i < 500; i++) {
        if (!(port_read(port, PORT_CMD) & PORT_CMD_FR)) {
            break;
        }
        for (volatile int j = 0; j < 10000; j++) {
            __asm__ volatile ("pause");
        }
    }
}

/* ── 端口初始化 ── */
static int port_init(int port) {
    printk("[AHCI] Initializing port %d...\n", port);

    /* 1. 停止端口 */
    port_stop(port);

    /* 2. 分配 Command List (1KB, 32 entry × 32 bytes, 对齐 1KB) */
    void *cl_p = pmm_alloc_page();
    if (!cl_p) { printk("[AHCI] OOM for cmd list!\n"); return -1; }
    port_cl = (ahci_cmd_header *)cl_p;
    port_cl_phys = (uint32_t)cl_p;

    /* 清零 */
    for (int i = 0; i < 1024 / 4; i++) {
        ((volatile uint32_t *)port_cl)[i] = 0;
    }

    /* 3. 分配 Received FIS (256B, 对齐 256B) */
    void *fis_p = pmm_alloc_page();
    if (!fis_p) { printk("[AHCI] OOM for recv FIS!\n"); return -1; }
    port_fis = (ahci_recv_fis *)fis_p;
    port_fis_phys = (uint32_t)fis_p;

    /* 清零 */
    for (int i = 0; i < 256 / 4; i++) {
        ((volatile uint32_t *)port_fis)[i] = 0;
    }

    /* 4. 分配 Command Table (每个 slot 一个, 初版只用 slot 0)
     *    Command Table 大小: 64+16+48+16 = 144 字节 (包含 1 个 PRDT)
     *    放在同一页中 (4KB 足够) */
    void *ct_p = pmm_alloc_page();
    if (!ct_p) { printk("[AHCI] OOM for cmd table!\n"); return -1; }
    port_ct = (ahci_cmd_table *)ct_p;
    port_ct_phys = (uint32_t)ct_p;

    /* 清零 */
    for (int i = 0; i < 256 / 4; i++) {
        ((volatile uint32_t *)port_ct)[i] = 0;
    }

    /* 5. 设置 Command List entry 0 → Command Table */
    port_cl[0].ctba  = port_ct_phys;
    port_cl[0].ctbau = 0;
    port_cl[0].prdt_entries = 0;  /* 稍后按需设置 */

    /* 6. 写 CLB 和 FB 到端口寄存器 */
    port_write(port, PORT_CLB,  port_cl_phys);
    port_write(port, PORT_CLBU, 0);
    port_write(port, PORT_FB,   port_fis_phys);
    port_write(port, PORT_FBU,  0);

    /* 7. 清中断状态 */
    port_write(port, PORT_IS, 0xFFFFFFFF);
    port_write(port, PORT_IE, 0);   /* 轮询模式, 不用中断 */

    /* 8. 开启端口: FRE + ST */
    uint32_t cmd = port_read(port, PORT_CMD);
    cmd |= PORT_CMD_FRE;
    port_write(port, PORT_CMD, cmd);

    /* 等待 FR 置位 */
    for (int i = 0; i < 500; i++) {
        if (port_read(port, PORT_CMD) & PORT_CMD_FR) break;
        for (volatile int j = 0; j < 10000; j++) {
            __asm__ volatile ("pause");
        }
    }

    /* 立即开启 ST */
    cmd = port_read(port, PORT_CMD);
    cmd |= PORT_CMD_ST;
    port_write(port, PORT_CMD, cmd);

    printk("[AHCI] Port %d initialized (CL=0x%x FIS=0x%x CT=0x%x)\n",
           port, port_cl_phys, port_fis_phys, port_ct_phys);
    return 0;
}

/* ── 等待端口就绪 ── */
static int port_wait_ready(int port) {
    /* 等 BSY 清零 */
    for (int i = 0; i < 1000; i++) {
        uint32_t tfd = port_read(port, PORT_TFD);
        if (!(tfd & (PORT_TFD_BSY | PORT_TFD_DRQ))) {
            return 0;
        }
        for (volatile int j = 0; j < 10000; j++) {
            __asm__ volatile ("pause");
        }
    }
    printk("[AHCI] Port %d wait ready timeout, tfd=0x%x\n",
           port, port_read(port, PORT_TFD));
    return -1;
}

/* ── 执行命令 (单 slot, 轮询) ── */
static int port_issue_cmd(int port, int is_write,
                          uint64_t lba, uint32_t count,
                          void *buf, uint8_t cmd_byte) {
    /* 确保端口空闲 */
    if (port_read(port, PORT_CI) != 0) {
        printk("[AHCI] Port %d busy! CI=0x%x\n", port, port_read(port, PORT_CI));
        return -1;
    }

    /* 清中断状态 */
    port_write(port, PORT_IS, 0xFFFFFFFF);

    /* ── 构建 Command FIS (H2D Register FIS) ── */
    volatile uint8_t *cfis = (volatile uint8_t *)port_ct->cfis;

    /* 清零 CFIS */
    for (int i = 0; i < 64; i++) cfis[i] = 0;

    /* FIS Type: 0x27 = H2D Register FIS, bit7=1 (Host to Device) */
    cfis[0] = 0x27 | 0x80;   /* FIS Type + C bit */

    /* Command byte */
    cfis[2] = cmd_byte;

    /* LBA (48-bit) */
    cfis[4]  = (uint8_t)(lba & 0xFF);          /* LBA[7:0]    */
    cfis[5]  = (uint8_t)((lba >> 8) & 0xFF);   /* LBA[15:8]   */
    cfis[6]  = (uint8_t)((lba >> 16) & 0xFF);  /* LBA[23:16]  */
    cfis[7]  = 0x40;                            /* Device: LBA mode */
    cfis[8]  = (uint8_t)((lba >> 24) & 0xFF);  /* LBA[31:24]  */
    cfis[9]  = (uint8_t)((lba >> 32) & 0xFF);  /* LBA[39:32]  */
    cfis[10] = (uint8_t)((lba >> 40) & 0xFF);  /* LBA[47:40]  */

    /* Sector count */
    cfis[12] = (uint8_t)(count & 0xFF);
    cfis[13] = (uint8_t)((count >> 8) & 0xFF);

    /* ── 设置 PRDT ── */
    port_ct->prdt[0].dba  = (uint32_t)buf;      /* 物理地址 */
    port_ct->prdt[0].dbau = 0;
    port_ct->prdt[0].dbc  = (count * 512) - 1;  /* byte count - 1 */
    port_ct->prdt[0].reserved = 0;

    /* ── 设置 Command Header ── */
    uint16_t cmd_flags = (uint16_t)(5);          /* CFL = 5 (FIS 长度 = 5 DWords = 20 bytes) */
    if (is_write) {
        cmd_flags |= AHCI_CMD_WRITE;              /* Write to device */
    }
    if (count > 0 && buf != nullptr) {
        cmd_flags |= 0;                           /* PRDT 存在... actually bit0-4 = CFL */
    }
    port_cl[0].flags  = cmd_flags;
    port_cl[0].prdt_entries = (buf && count > 0) ? 1 : 0;
    port_cl[0].prdbc = 0;

    /* 确保写入内存 (在发 CI 之前) */
    __asm__ volatile ("" : : : "memory");

    /* ── 发命令 ── */
    port_write(port, PORT_CI, 1);  /* 发 slot 0 */

    /* ── 等待完成 ── */
    for (int i = 0; i < 10000; i++) {
        if ((port_read(port, PORT_CI) & 1) == 0) {
            /* 检查错误 */
            uint32_t tfd = port_read(port, PORT_TFD);
            if (tfd & PORT_TFD_ERR) {
                printk("[AHCI] Cmd 0x%x failed, TFD=0x%x\n", cmd_byte, tfd);
                return -1;
            }
            return 0;
        }
        for (volatile int j = 0; j < 1000; j++) {
            __asm__ volatile ("pause");
        }
    }

    printk("[AHCI] Cmd 0x%x timeout! CI=0x%x TFD=0x%x\n",
           cmd_byte, port_read(port, PORT_CI), port_read(port, PORT_TFD));
    return -1;
}

/* ── IDENTIFY DEVICE ── */
static int do_identify(int port, uint16_t *id_buf) {
    /* 分配 512B 缓冲区 (物理连续, 对齐) */
    void *buf = pmm_alloc_page();
    if (!buf) return -1;

    int rc = port_issue_cmd(port, 0, 0, 1, buf, 0xEC);  /* 0xEC = IDENTIFY */
    if (rc != 0) {
        pmm_free_page(buf);
        return rc;
    }

    /* 复制结果 */
    for (int i = 0; i < 256; i++) {
        id_buf[i] = ((uint16_t *)buf)[i];
    }
    pmm_free_page(buf);
    return 0;
}

/* ── 从 IDENTIFY 数据中提取字符串 (word-swapped) ── */
static void id_string(uint16_t *id, int start_word, int len, char *out) {
    for (int i = 0; i < len / 2; i++) {
        uint16_t w = id[start_word + i];
        out[i * 2]     = (char)((w >> 8) & 0xFF);
        out[i * 2 + 1] = (char)(w & 0xFF);
    }
    out[len] = '\0';
    /* 去除尾部空格 */
    for (int i = len - 1; i >= 0; i--) {
        if (out[i] == ' ') out[i] = '\0';
        else break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 公开 API
 * ═══════════════════════════════════════════════════════════════════════════ */

int ahci_init() {
    serial_print("[AHCI] Searching for AHCI controller...\n");
    printk("\n[AHCI] Searching for AHCI controller...\n");

    /* ── 调试: 直接读 bus0 dev0 func0 ── */
    serial_print("[AHCI] Raw PCI: b0d0f0 off0=");
    serial_print_hex(pci_config_read(0, 0, 0, 0));
    serial_print(" off8=");
    serial_print_hex(pci_config_read(0, 0, 0, 8));
    serial_print("\n");
    serial_print("[AHCI] Raw PCI: b0d2f0 off0=");
    serial_print_hex(pci_config_read(0, 2, 0, 0));
    serial_print(" off8=");
    serial_print_hex(pci_config_read(0, 2, 0, 8));
    serial_print("\n");

    /* 1. PCI 扫描找 AHCI (class=0x01 storage, subclass=0x06 SATA) */
    pci_dev devs[4];
    int n = pci_scan(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, devs, 4);
    if (n == 0) {
        serial_print("[AHCI] No AHCI controller found!\n");
        printk("[AHCI] No AHCI controller found!\n");
        return -1;
    }

    pci_dev *ahci_dev = &devs[0];
    serial_print("[AHCI] Found controller via PCI scan\n");
    printk("[AHCI] Found: %04x:%04x at %02x:%02x.%x IRQ=%d\n",
           ahci_dev->vendor_id, ahci_dev->device_id,
           ahci_dev->bus, ahci_dev->dev, ahci_dev->func,
           ahci_dev->irq_line);

    /* 2. 从 BAR[5] 获取 ABAR 物理地址 */
    uint32_t abar_phys = ahci_dev->bar[5] & 0xFFFFFFF0;
    serial_putchar('\n');
    if (abar_phys == 0) {
        serial_putchar('E'); serial_putchar('\n');
        printk("[AHCI] BAR[5] is zero! Not an AHCI controller?\n");
        return -1;
    }

    hba_phys = abar_phys;

    /* 3. 映射 ABAR MMIO 区域 */
    uint32_t page_aligned = abar_phys & PAGE_MASK;
    if (page_aligned < 0x400000) {
        hba_base = (volatile uint32_t *)abar_phys;
    } else {
        for (uint32_t va = page_aligned; va < abar_phys + 0x1100; va += PAGE_SIZE) {
            int rc = vmm_map_page(va, va, PAGE_PRESENT | PAGE_RW);
            if (rc != 0) {
                serial_print("[AHCI] Failed to map ABAR page!\n");
                return -1;
            }
        }
        hba_base = (volatile uint32_t *)abar_phys;
    }
    serial_putchar('M'); serial_putchar('\n');

    /* 4. 验证 ABAR: 读 CAP 寄存器 */
    uint32_t cap = hba_read(0x00);
    hba_port_count = (int)((cap & HBA_CAP_NP_MASK) + 1);
    printk("[AHCI] CAP=0x%x  Ports=%d  S64A=%d\n",
           cap, hba_port_count, (cap & HBA_CAP_S64A) ? 1 : 0);

    /* 5. HBA 复位 */
    serial_print("[AHCI] HBA reset...\n");
    if (hba_reset() != 0) return -1;
    serial_print("[AHCI] HBA reset done\n");

    /* 6. 扫描 port */
    uint32_t pi = hba_read(0x0C);
    serial_print("[AHCI] PI=");
    serial_print_hex(pi);
    serial_print("\n");
    printk("[AHCI] Ports implemented: 0x%x\n", pi);

    active_port = -1;
    for (int p = 0; p < hba_port_count; p++) {
        if (!(pi & (1U << p))) continue;

        /* 检查设备是否存在 */
        uint32_t ssts = port_read(p, PORT_SSTS);
        uint8_t det = ssts & PORT_SSTS_DET_MASK;
        if (det != PORT_SSTS_DET_PRES) {
            printk("[AHCI] Port %d: no device (SSTS=0x%x)\n", p, ssts);
            continue;
        }

        printk("[AHCI] Port %d: device present (SSTS=0x%x)\n", p, ssts);

        /* 初始化这个 port */
        serial_print("[AHCI] Initializing port...\n");
        if (port_init(p) != 0) {
            serial_print("[AHCI] Port init FAILED!\n");
            printk("[AHCI] Port %d init failed!\n", p);
            continue;
        }
        serial_print("[AHCI] Port init OK, sending IDENTIFY...\n");

        /* IDENTIFY */
        uint16_t id_buf[256];
        if (do_identify(p, id_buf) != 0) {
            serial_print("[AHCI] IDENTIFY FAILED!\n");
            printk("[AHCI] Port %d IDENTIFY failed!\n", p);
            continue;
        }
        serial_print("[AHCI] IDENTIFY OK!\n");

        char model[41];
        char serial[21];
        id_string(id_buf, IDENT_MODEL, 40, model);
        id_string(id_buf, IDENT_SERIAL, 20, serial);

        /* LBA48 扇区数 */
        uint64_t sectors = 0;
        if (id_buf[IDENT_CAPS] & (1 << 10)) {  /* LBA48 supported */
            sectors = ((uint64_t)id_buf[IDENT_LBA48 + 1] << 32)
                    | ((uint64_t)id_buf[IDENT_LBA48]      )
                    | ((uint64_t)id_buf[IDENT_LBA48 + 3] << 48)
                    | ((uint64_t)id_buf[IDENT_LBA48 + 2] << 16);
        } else {
            /* LBA28 fallback: read words 60-61 */
            sectors = id_buf[60] | ((uint32_t)id_buf[61] << 16);
        }

        printk("[AHCI] Port %d: \"%s\"  S/N:%s  %d sectors (%d MB)\n",
               p, model, serial,
               (uint32_t)sectors, (uint32_t)(sectors / 2048));

        active_port = p;
        disk_sectors = sectors;
        break;  /* 只用第一个找到的设备 */
    }

    if (active_port < 0) {
        serial_print("[AHCI] No usable ports found!\n");
        printk("[AHCI] No usable ports found!\n");
        return -1;
    }

    serial_print("[AHCI] Init complete!\n");
    printk("[AHCI] Init OK, using port %d, %d sectors\n",
           active_port, (uint32_t)disk_sectors);
    return 0;
}

int ahci_identify(char *model_out, char *serial_out, uint64_t *total_sectors) {
    if (active_port < 0) return -1;

    uint16_t id_buf[256];
    if (do_identify(active_port, id_buf) != 0) return -1;

    if (model_out)  id_string(id_buf, IDENT_MODEL, 40, model_out);
    if (serial_out) id_string(id_buf, IDENT_SERIAL, 20, serial_out);
    if (total_sectors) {
        if (id_buf[IDENT_CAPS] & (1 << 10)) {
            *total_sectors = ((uint64_t)id_buf[IDENT_LBA48 + 1] << 32)
                           | ((uint64_t)id_buf[IDENT_LBA48]      )
                           | ((uint64_t)id_buf[IDENT_LBA48 + 3] << 48)
                           | ((uint64_t)id_buf[IDENT_LBA48 + 2] << 16);
        } else {
            *total_sectors = id_buf[60] | ((uint32_t)id_buf[61] << 16);
        }
    }
    return 0;
}

int ahci_read(uint64_t lba, uint32_t count, void *buf) {
    if (active_port < 0) return -1;
    if (count == 0) return 0;

    /* buf 必须是物理地址 (当前 identity-mapped, 虚拟=物理) */
    return port_issue_cmd(active_port, 0, lba, count, buf, 0x25);
}

int ahci_write(uint64_t lba, uint32_t count, const void *buf) {
    if (active_port < 0) return -1;
    if (count == 0) return 0;

    return port_issue_cmd(active_port, 1, lba, count, (void *)buf, 0x35);
}
