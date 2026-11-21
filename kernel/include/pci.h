#ifndef PCI_H
#define PCI_H

#include "types.h"

#define PCI_MAX_DEVICES 32

/* One function found on the bus (configuration mechanism #1). */
typedef struct
{
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;    /* without the multi-function bit */

    uint8_t irq_line;       /* 0xFF = none */
    uint8_t irq_pin;        /* 0 = none, 1~4 = INTA~INTD */

    uint32_t bar[6];        /* raw BARs (header type 0 only) */
} pci_device_t;

/* Scans every bus/slot/function and records what it finds. */
void pci_init(void);

int pci_device_count(void);

/* Device i (0 <= i < pci_device_count()), or 0. */
const pci_device_t *pci_get_device(int i);

/* First device with this class/subclass (0xFF = any subclass), or 0. */
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);

/* Human-readable class name, e.g. "IDE controller". */
const char *pci_class_name(const pci_device_t *dev);

/* Known vendor names (Intel, QEMU/Bochs, ...), or 0. */
const char *pci_vendor_name(uint16_t vendor_id);

/* Configuration space access (offset is rounded down to 4 bytes). */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint32_t value);

#endif
