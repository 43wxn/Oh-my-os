/* ============================================================================
 * kernel/drivers/pci/pci.cc — PCI 总线枚举
 *
 * PCI 配置空间通过端口 0xCF8 (地址) 和 0xCFC (数据) 访问。
 *
 * 地址格式 (32-bit, 写入 0xCF8):
 *   bit31      = Enable (1)
 *   bit30-24   = 保留 (0)
 *   bit23-16   = Bus
 *   bit15-11   = Device
 *   bit10-8    = Function
 *   bit7-2     = Register Offset
 *   bit1-0     = 0 (必须对齐)
 * ============================================================================ */

#include "kernel/drivers/pci/pci.h"
#include "kernel/arch/x86/port.h"
#include "kernel/printk.h"
#include "kernel/lib/serial.h"

#define PCI_ADDR_PORT  0xCF8
#define PCI_DATA_PORT  0xCFC

/* ── 构造 PCI 配置空间地址 ── */
static inline uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (1U << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)func << 8)
         | ((uint32_t)offset & 0xFC);
}

/* ── 读 32-bit ── */
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_ADDR_PORT, pci_addr(bus, dev, func, offset));
    return inl(PCI_DATA_PORT);
}

/* ── 读 16-bit ── */
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_config_read(bus, dev, func, offset & 0xFC);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

/* ── 写 32-bit ── */
void pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_ADDR_PORT, pci_addr(bus, dev, func, offset));
    outl(PCI_DATA_PORT, val);
}

/* ── 读取设备信息 ── */
static void pci_read_dev(uint8_t bus, uint8_t dev, uint8_t func, pci_dev *out) {
    out->vendor_id = pci_config_read16(bus, dev, func, PCI_VENDOR_ID);
    out->device_id = pci_config_read16(bus, dev, func, PCI_DEVICE_ID);
    /* 偏移 0x08 DWORD = [RevID:0x08][ProgIF:0x09][Subclass:0x0A][Class:0x0B] */
    uint32_t class_dword = pci_config_read(bus, dev, func, 0x08);
    out->class_code = (uint8_t)(class_dword >> 24);     /* byte 3 = Class     */
    out->subclass   = (uint8_t)(class_dword >> 16);     /* byte 2 = Subclass  */
    out->prog_if    = (uint8_t)(class_dword >> 8);      /* byte 1 = Prog IF   */
    out->bus        = bus;
    out->dev        = dev;
    out->func       = func;
    out->irq_line   = (uint8_t)pci_config_read16(bus, dev, func, PCI_IRQ_LINE);

    /* 读 BAR[0..5] */
    for (int i = 0; i < 6; i++) {
        out->bar[i] = pci_config_read(bus, dev, func, PCI_BAR0 + i * 4);
    }
}

/* ── 类名 (常见) ── */
static const char* class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01:
        switch (subclass) {
        case 0x01: return "IDE";
        case 0x06: return "AHCI/SATA";
        default:   return "Storage";
        }
    case 0x02: return "Network";
    case 0x03:
        switch (subclass) {
        case 0x00: return "VGA";
        default:   return "Display";
        }
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        default:   return "Bridge";
        }
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB";
        default:   return "Serial Bus";
        }
    default: return "Other";
    }
}

/* ── 扫描总线 ── */
int pci_scan(uint8_t class_code, uint8_t subclass, pci_dev *devs, int max) {
    int count = 0;

    /* 扫描 bus 0 和 bus 1 (QEMU 有时把 AHCI 放 bus 1) */
    for (uint8_t bus = 0; bus < 2; bus++) {
        printk("[PCI] Scanning bus %d for class=%02x subclass=%02x\n",
               bus, class_code, subclass);
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_config_read16(bus, dev, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;           /* 设备不存在          */

            uint8_t hdr_type = (uint8_t)pci_config_read16(bus, dev, 0, PCI_HEADER_TYPE);
            uint8_t max_func = (hdr_type & 0x80) ? 8 : 1;  /* 多功能设备?  */

            for (uint8_t func = 0; func < max_func; func++) {
                vendor = pci_config_read16(bus, dev, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFF) continue;

                pci_dev d;
                pci_read_dev(bus, dev, func, &d);

                /* 调试: 打印前 10 个设备到串口 */
                static int dbg_count = 0;
                if (dbg_count < 10) {
                    serial_print("  [");
                    serial_putchar("0123456789abcdef"[(d.bus >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.bus & 0xF]);
                    serial_print(":");
                    serial_putchar("0123456789abcdef"[(d.dev >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.dev & 0xF]);
                    serial_print(".");
                    serial_putchar("0123456789abcdef"[d.func]);
                    serial_print("] vid=");
                    serial_putchar("0123456789abcdef"[(d.vendor_id >> 12) & 0xF]);
                    serial_putchar("0123456789abcdef"[(d.vendor_id >> 8) & 0xF]);
                    serial_putchar("0123456789abcdef"[(d.vendor_id >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.vendor_id & 0xF]);
                    serial_print(" did=");
                    serial_putchar("0123456789abcdef"[(d.device_id >> 12) & 0xF]);
                    serial_putchar("0123456789abcdef"[(d.device_id >> 8) & 0xF]);
                    serial_putchar("0123456789abcdef"[(d.device_id >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.device_id & 0xF]);
                    serial_print(" cls=");
                    serial_putchar("0123456789abcdef"[(d.class_code >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.class_code & 0xF]);
                    serial_print(" sub=");
                    serial_putchar("0123456789abcdef"[(d.subclass >> 4) & 0xF]);
                    serial_putchar("0123456789abcdef"[d.subclass & 0xF]);
                    serial_print("\n");
                    dbg_count++;
                }

                if (d.class_code == class_code && d.subclass == subclass) {
                    if (count < max) {
                        devs[count] = d;
                    }
                    count++;
                }
            }
        }
    }

    return count;
}

/* ── 打印所有设备 (调试) ── */
void pci_dump() {
    printk("\n=== PCI Devices ===\n");
    printk("Bus:Dev:Fn  Vendor:Device  Class        IRQ  BAR0\n");

    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_config_read16(bus, dev, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;

            uint8_t hdr_type = (uint8_t)pci_config_read16(bus, dev, 0, PCI_HEADER_TYPE);
            uint8_t max_func = (hdr_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                vendor = pci_config_read16(bus, dev, func, PCI_VENDOR_ID);
                if (vendor == 0xFFFF) continue;

                pci_dev d;
                pci_read_dev(bus, dev, func, &d);

                printk(" %02x:%02x:%x     %04x:%04x      %-12s  %-3d  0x%x\n",
                       d.bus, d.dev, d.func,
                       d.vendor_id, d.device_id,
                       class_name(d.class_code, d.subclass),
                       d.irq_line, d.bar[0]);
            }
        }
    }

    /* Scan bus 1 as well (QEMU sometimes puts AHCI there) */
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(1, dev, 0, PCI_VENDOR_ID);
        if (vendor == 0xFFFF) continue;
        pci_dev d;
        pci_read_dev(1, dev, 0, &d);
        printk(" %02x:%02x:%x     %04x:%04x      %-12s  %-3d  0x%x\n",
               d.bus, d.dev, d.func,
               d.vendor_id, d.device_id,
               class_name(d.class_code, d.subclass),
               d.irq_line, d.bar[0]);
    }

    printk("\n");
}
