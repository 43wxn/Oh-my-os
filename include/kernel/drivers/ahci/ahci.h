/* ============================================================================
 * kernel/drivers/ahci/ahci.h — AHCI SATA 驱动
 *
 * 支持: HBA 初始化, Port 探测, IDENTIFY, 读写扇区 (LBA48)
 * 模式: 轮询 (无中断), 单 command slot
 * ============================================================================ */

#ifndef _KERNEL_DRIVERS_AHCI_AHCI_H
#define _KERNEL_DRIVERS_AHCI_AHCI_H

#include <stdint.h>

/* ── HBA 寄存器 (Host Bus Adapter, 通过 ABAR MMIO 访问) ── */

/* HBA Capabilities (偏移 0x00) */
#define HBA_CAP_NP_MASK   0x1F        /* Number of Ports (bits 4:0)      */
#define HBA_CAP_S64A      (1U << 31)  /* 64-bit addressing supported     */

/* Global Host Control (偏移 0x04) */
#define HBA_GHC_HR        (1U << 0)   /* HBA Reset                       */
#define HBA_GHC_IE        (1U << 1)   /* Interrupt Enable                */
#define HBA_GHC_AE        (1U << 31)  /* AHCI Enable                     */

/* Port CMD (偏移 port+0x18) */
#define PORT_CMD_ST       (1U << 0)   /* Start                           */
#define PORT_CMD_SUD      (1U << 1)   /* Spin-Up Device                  */
#define PORT_CMD_POD      (1U << 2)   /* Power On Device                 */
#define PORT_CMD_FRE      (1U << 4)   /* FIS Receive Enable              */
#define PORT_CMD_FR       (1U << 14)  /* FIS Receive Running (RO)        */
#define PORT_CMD_CR       (1U << 15)  /* Command List Running (RO)       */

/* Port SATA Status (偏移 port+0x28) */
#define PORT_SSTS_DET_MASK  0x0F      /* Device Detection               */
#define PORT_SSTS_DET_NONE  0x00      /*   No device                    */
#define PORT_SSTS_DET_PRES  0x03      /*   Device present, comm OK      */

/* Port Task File Data (偏移 port+0x20) */
#define PORT_TFD_ERR       (1U << 0)  /* Error in task file             */
#define PORT_TFD_BSY       (1U << 7)  /* Device busy                    */
#define PORT_TFD_DRQ       (1U << 3)  /* Data request                   */

/* ── AHCI 内存数据结构 ── */

/* Command Header (HBA → 驱动, 每个 port 最多 32 个 slot) */
struct ahci_cmd_header {
    uint16_t flags;          /* bit0=CFL(5-bit), bit5=write, bit6=prefetch */
    uint16_t prdt_entries;   /* PRDT 条目数 (最大 65535)                   */
    uint32_t prdbc;          /* 实际传输字节数 (written by HBA)             */
    uint32_t ctba;           /* Command Table 基址 (物理地址低32位)         */
    uint32_t ctbau;          /* Command Table 基址 (物理地址高32位, =0)     */
    uint32_t reserved[4];
};

#define AHCI_CMD_WRITE   (1U << 6)   /* Write to device (0=read)         */

/* Command Table */
struct ahci_cmd_table {
    uint8_t  cfis[64];       /* Command FIS (Frame Information Structure)  */
    uint8_t  acmd[16];       /* ATAPI Command (不用)                      */
    uint8_t  reserved[48];
    struct {
        uint32_t dba;        /* Data Base Address (物理地址低32位)         */
        uint32_t dbau;       /* Data Base Address (高32位, =0)            */
        uint32_t reserved;
        uint32_t dbc;        /* Byte Count (bit31=interrupt on complete)   */
    } prdt[1];               /* PRDT entries (初版只用 1 个)               */
};

#define PRDT_IOC         (1U << 31)  /* Interrupt On Complete             */

/* Received FIS (从设备收到的, 由 HBA 写入, 驱动读取) */
struct ahci_recv_fis {
    uint8_t  dsfis[28];      /* DMA Setup FIS                             */
    uint8_t  reserved1[4];
    uint8_t  psfis[24];      /* PIO Setup FIS                             */
    uint8_t  reserved2[8];
    uint8_t  rfis[24];       /* D2H Register FIS                          */
    uint8_t  reserved3[4];
    uint8_t  sdbfis[8];      /* Set Device Bits FIS                       */
    uint8_t  ufis[64];       /* Unknown FIS                               */
    uint8_t  reserved4[96];
};

/* IDENTIFY DEVICE 返回的数据 (512B, 只定义我们关心的偏移) */
#define IDENT_SERIAL    10    /* 序列号 (20 chars, word 10-19)            */
#define IDENT_MODEL     27    /* 型号     (40 chars, word 27-46)          */
#define IDENT_LBA48     100   /* LBA48 最大地址 (QWord, word 100-103)     */
#define IDENT_CAPS      83    /* bit10=1 → LBA48 支持                     */

/* ── Port 寄存器布局 (每个 port 的 MMIO 偏移) ── */
#define PORT_CLB        0x00  /* Command List Base Address                */
#define PORT_CLBU       0x04
#define PORT_FB         0x08  /* FIS Base Address                         */
#define PORT_FBU        0x0C
#define PORT_IS         0x10  /* Interrupt Status                         */
#define PORT_IE         0x14  /* Interrupt Enable                         */
#define PORT_CMD        0x18  /* Command & Status                         */
#define PORT_RESERVED0  0x1C
#define PORT_TFD        0x20  /* Task File Data                           */
#define PORT_SIG        0x24  /* Signature                                */
#define PORT_SSTS       0x28  /* SATA Status                              */
#define PORT_SCTL       0x2C  /* SATA Control                             */
#define PORT_SERR       0x30  /* SATA Error                               */
#define PORT_SACT       0x34  /* SATA Active                              */
#define PORT_CI         0x38  /* Command Issue                            */
#define PORT_SNTF       0x3C  /* SATA Notification                        */

#define PORT_SIZE       0x80  /* Total port register space                */

/* ── HBA Memory 整体布局 ── */
struct hba_mem {
    /* 0x00 - 0x2B: Generic Host Control */
    uint32_t cap;              /* 0x00  Host Capability                    */
    uint32_t ghc;              /* 0x04  Global Host Control                */
    uint32_t is;               /* 0x08  Interrupt Status                   */
    uint32_t pi;               /* 0x0C  Ports Implemented                  */
    uint32_t vs;               /* 0x10  Version                            */
    uint32_t ccc_ctl;          /* 0x14  Command Completion Coalescing      */
    uint32_t ccc_pts;          /* 0x18                                       */
    uint32_t em_loc;           /* 0x1C  Enclosure Management               */
    uint32_t em_ctl;           /* 0x20                                       */
    uint32_t cap2;             /* 0x24  Host Capabilities Extended         */
    uint32_t bohc;             /* 0x28  BIOS/OS Handoff Control            */
    uint8_t  reserved[0x74];   /* 0x2C - 0x9F                             */
    uint8_t  vendor[0x60];     /* 0xA0 - 0xFF  Vendor Specific             */
    /* 0x100 - 0x10FF: Port 0..31 Registers */
    uint8_t  ports[0x1000 - 0x100];
};

/* 获取 port i 的寄存器指针 */
static inline volatile uint8_t* hba_port_base(struct hba_mem *hba, int port) {
    return &hba->ports[port * PORT_SIZE];
}

static inline volatile uint32_t* hba_port_reg(struct hba_mem *hba,
                                               int port, uint32_t offset) {
    return (volatile uint32_t*)(&hba->ports[port * PORT_SIZE + offset]);
}

/* ── API ── */

/* 初始化 AHCI 驱动 (扫描 PCI, 初始化第一个找到的 AHCI 控制器) */
int ahci_init();

/* 获取数据盘信息 (port 0) */
int ahci_identify(char *model_out, char *serial_out, uint64_t *total_sectors);

/* 扇区读写 (port 0, LBA48) */
int ahci_read(uint64_t lba, uint32_t count, void *buf);
int ahci_write(uint64_t lba, uint32_t count, const void *buf);

#endif /* _KERNEL_DRIVERS_AHCI_AHCI_H */
