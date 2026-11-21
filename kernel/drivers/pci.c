#include "../include/pci.h"
#include "../include/io.h"
#include "../include/cpu.h"
#include "../include/printk.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_NONE    0xFFFF
#define PCI_MULTIFUNCTION  0x80

#define PCI_CLASS_BRIDGE   0x06
#define PCI_SUBCLASS_PCI   0x04

static pci_device_t devices[PCI_MAX_DEVICES];
static int device_count = 0;

static uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) | (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t flags = irq_save();

    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, offset));
    uint32_t value = inl(PCI_CONFIG_DATA);

    irq_restore(flags);

    return value;
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint32_t value)
{
    uint32_t flags = irq_save();

    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);

    irq_restore(flags);
}

static uint16_t vendor_of(uint8_t bus, uint8_t slot, uint8_t func)
{
    return (uint16_t)(pci_config_read32(bus, slot, func, 0x00) & 0xFFFF);
}

static void scan_bus(uint8_t bus, int depth);

static void add_function(uint8_t bus, uint8_t slot, uint8_t func, int depth)
{
    uint32_t id = pci_config_read32(bus, slot, func, 0x00);
    uint32_t class_reg = pci_config_read32(bus, slot, func, 0x08);
    uint32_t header = pci_config_read32(bus, slot, func, 0x0C);
    uint32_t irq = pci_config_read32(bus, slot, func, 0x3C);

    uint8_t class_code = (uint8_t)(class_reg >> 24);
    uint8_t subclass = (uint8_t)(class_reg >> 16);

    if (device_count < PCI_MAX_DEVICES)
    {
        pci_device_t *dev = &devices[device_count++];

        dev->bus = bus;
        dev->slot = slot;
        dev->func = func;
        dev->vendor_id = (uint16_t)(id & 0xFFFF);
        dev->device_id = (uint16_t)(id >> 16);
        dev->class_code = class_code;
        dev->subclass = subclass;
        dev->prog_if = (uint8_t)(class_reg >> 8);
        dev->revision = (uint8_t)class_reg;
        dev->header_type = (uint8_t)((header >> 16) & 0x7F);
        dev->irq_line = (uint8_t)irq;
        dev->irq_pin = (uint8_t)(irq >> 8);

        if (dev->header_type == 0)
        {
            for (int i = 0; i < 6; i++)
                dev->bar[i] = pci_config_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
        }
    }

    /* PCI-to-PCI bridge: its secondary bus number is in byte 1 of 0x18. */
    if (class_code == PCI_CLASS_BRIDGE && subclass == PCI_SUBCLASS_PCI && depth < 8)
    {
        uint8_t secondary = (uint8_t)(pci_config_read32(bus, slot, func, 0x18) >> 8);

        if (secondary > bus)
            scan_bus(secondary, depth + 1);
    }
}

static void scan_slot(uint8_t bus, uint8_t slot, int depth)
{
    if (vendor_of(bus, slot, 0) == PCI_VENDOR_NONE)
        return;

    add_function(bus, slot, 0, depth);

    uint8_t header = (uint8_t)(pci_config_read32(bus, slot, 0, 0x0C) >> 16);

    if (!(header & PCI_MULTIFUNCTION))
        return;

    for (uint8_t func = 1; func < 8; func++)
    {
        if (vendor_of(bus, slot, func) != PCI_VENDOR_NONE)
            add_function(bus, slot, func, depth);
    }
}

static void scan_bus(uint8_t bus, int depth)
{
    for (uint8_t slot = 0; slot < 32; slot++)
        scan_slot(bus, slot, depth);
}

void pci_init(void)
{
    device_count = 0;

    /* Mechanism #1 present? The address register reads back what was written. */
    outl(PCI_CONFIG_ADDRESS, 0x80000000u);

    if (inl(PCI_CONFIG_ADDRESS) != 0x80000000u)
    {
        klog("[pci] no configuration mechanism #1\n");
        return;
    }

    /* Bus 0 and whatever hangs off its bridges (recursive scan). */
    scan_bus(0, 0);

    klog("[pci] %d device(s)\n", device_count);

    for (int i = 0; i < device_count; i++)
    {
        const pci_device_t *d = &devices[i];

        klog("[pci]   %02x:%02x.%u %04x:%04x %s\n", d->bus, d->slot, d->func,
             d->vendor_id, d->device_id, pci_class_name(d));
    }
}

int pci_device_count(void)
{
    return device_count;
}

const pci_device_t *pci_get_device(int i)
{
    if (i < 0 || i >= device_count)
        return 0;

    return &devices[i];
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass)
{
    for (int i = 0; i < device_count; i++)
    {
        if (devices[i].class_code == class_code &&
            (subclass == 0xFF || devices[i].subclass == subclass))
            return &devices[i];
    }

    return 0;
}

const char *pci_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id)
    {
        case 0x8086: return "Intel";
        case 0x1234: return "QEMU/Bochs";
        case 0x1AF4: return "Red Hat (virtio)";
        case 0x10EC: return "Realtek";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x15AD: return "VMware";
        case 0x80EE: return "VirtualBox";
        case 0x1B36: return "Red Hat (QEMU)";
        default:     return 0;
    }
}

typedef struct
{
    uint8_t class_code;
    uint8_t subclass;   /* 0xFF = any */
    const char *name;
} class_name_t;

static const class_name_t class_names[] =
{
    { 0x01, 0x01, "IDE controller" },
    { 0x01, 0x06, "SATA controller" },
    { 0x01, 0x08, "NVMe controller" },
    { 0x01, 0xFF, "Storage controller" },
    { 0x02, 0x00, "Ethernet controller" },
    { 0x02, 0xFF, "Network controller" },
    { 0x03, 0x00, "VGA controller" },
    { 0x03, 0xFF, "Display controller" },
    { 0x04, 0x01, "Audio device" },
    { 0x04, 0x03, "HD Audio controller" },
    { 0x04, 0xFF, "Multimedia device" },
    { 0x05, 0xFF, "Memory controller" },
    { 0x06, 0x00, "Host bridge" },
    { 0x06, 0x01, "ISA bridge" },
    { 0x06, 0x04, "PCI bridge" },
    { 0x06, 0x80, "Bridge" },
    { 0x06, 0xFF, "Bridge" },
    { 0x07, 0xFF, "Communication controller" },
    { 0x08, 0xFF, "System peripheral" },
    { 0x09, 0xFF, "Input device" },
    { 0x0C, 0x03, "USB controller" },
    { 0x0C, 0x05, "SMBus controller" },
    { 0x0C, 0xFF, "Serial bus controller" },
    { 0x0D, 0xFF, "Wireless controller" },
};

const char *pci_class_name(const pci_device_t *dev)
{
    for (uint32_t i = 0; i < sizeof(class_names) / sizeof(class_names[0]); i++)
    {
        const class_name_t *c = &class_names[i];

        if (c->class_code == dev->class_code &&
            (c->subclass == 0xFF || c->subclass == dev->subclass))
            return c->name;
    }

    return "Unknown device";
}
