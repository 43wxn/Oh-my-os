/* ============================================================================
 * kernel/drivers/pci/pci.h — PCI 总线枚举
 *
 * 扫描 PCI 配置空间, 查找设备。
 * 配置空间访问: 写 bus/dev/func/offset 到端口 0xCF8, 读/写端口 0xCFC。
 * ============================================================================ */

#ifndef _KERNEL_DRIVERS_PCI_PCI_H
#define _KERNEL_DRIVERS_PCI_PCI_H

#include <stdint.h>

/* PCI 配置空间偏移 (前 64 字节 header) */
#define PCI_VENDOR_ID     0x00
#define PCI_DEVICE_ID     0x02
#define PCI_COMMAND       0x04
#define PCI_STATUS        0x06
#define PCI_REVISION      0x08
#define PCI_PROG_IF       0x09
#define PCI_SUBCLASS      0x0A
#define PCI_CLASS         0x0B
#define PCI_HEADER_TYPE   0x0E
#define PCI_BAR0          0x10
#define PCI_BAR1          0x14
#define PCI_BAR2          0x18
#define PCI_BAR3          0x1C
#define PCI_BAR4          0x20
#define PCI_BAR5          0x24
#define PCI_IRQ_LINE      0x3C

/* 设备类型 */
#define PCI_CLASS_STORAGE 0x01    /* Mass Storage Controller      */
#define PCI_SUBCLASS_SATA 0x06    /* Serial ATA (AHCI)            */

/* 设备描述 */
struct pci_dev {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint32_t bar[6];
    uint8_t  irq_line;
};

/* ── API ── */
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

/* 扫描总线, 返回匹配 class/subclass 的设备数量。
 * devs: 输出数组 (调用者分配), max: 数组容量 */
int      pci_scan(uint8_t class_code, uint8_t subclass, pci_dev *devs, int max);

/* 打印所有 PCI 设备 (调试用) */
void     pci_dump();

#endif /* _KERNEL_DRIVERS_PCI_PCI_H */
