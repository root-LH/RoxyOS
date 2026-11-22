#include "../include/acpi.h"
#include "../include/io.h"
#include "../include/cpu.h"
#include "../include/paging.h"
#include "../include/string.h"
#include "../include/printk.h"

/* Tables above the physical map are mapped here, one page at a time. */
#define ACPI_WINDOW_START 0xF0000000
#define ACPI_WINDOW_PAGES 256

#define SLP_EN (1u << 13)
#define SCI_EN 1u

typedef struct
{
    char signature[8];      /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) sdt_header_t;

/* The part of the FADT (ACPI 1.0 layout) that is used here. */
typedef struct
{
    sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
} __attribute__((packed)) fadt_t;

static int acpi_ok = 0;
static uint32_t smi_command;
static uint8_t acpi_enable_value;
static uint16_t pm1a_control, pm1b_control;
static uint16_t slp_typ_a, slp_typ_b;

static uint32_t window_next = ACPI_WINDOW_START;

/*
 * A kernel pointer to physical [phys, phys + len). Low memory comes from
 * the physical map; anything else is mapped into the ACPI window (and
 * stays mapped: this only runs once, at boot).
 */
static const void *acpi_map(uint32_t phys, uint32_t len)
{
    if (phys + len >= phys && phys + len <= KERNEL_PHYS_MAP_SIZE)
        return (const void *)P2V(phys);

    uint32_t first = phys & ~0xFFFu;
    uint32_t last = (phys + len + 0xFFF) & ~0xFFFu;
    uint32_t pages = (last - first) / 4096;

    if (last < first || window_next + pages * 4096 >
        ACPI_WINDOW_START + ACPI_WINDOW_PAGES * 4096)
        return 0;

    uint32_t virt = window_next;

    for (uint32_t i = 0; i < pages; i++)
        map_page(virt + i * 4096, first + i * 4096, PAGE_PRESENT);

    window_next += pages * 4096;

    return (const void *)(virt + (phys - first));
}

static int checksum_ok(const void *data, uint32_t len)
{
    const uint8_t *p = data;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < len; i++)
        sum += p[i];

    return sum == 0;
}

/* Maps a whole table: the header first, then its full length. */
static const sdt_header_t *map_table(uint32_t phys)
{
    const sdt_header_t *h = acpi_map(phys, sizeof(sdt_header_t));

    if (!h || h->length < sizeof(sdt_header_t) || h->length > 1024 * 1024)
        return 0;

    h = acpi_map(phys, h->length);

    if (!h || !checksum_ok(h, h->length))
        return 0;

    return h;
}

static const rsdp_t *scan_rsdp(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr + sizeof(rsdp_t) <= end; addr += 16)
    {
        const rsdp_t *r = (const rsdp_t *)P2V(addr);

        if (memcmp(r->signature, "RSD PTR ", 8) == 0 && checksum_ok(r, sizeof(rsdp_t)))
            return r;
    }

    return 0;
}

/* The RSDP is in the first KB of the EBDA or in the BIOS area 0xE0000~0xFFFFF. */
static const rsdp_t *find_rsdp(void)
{
    uint32_t ebda = (uint32_t)(*(const uint16_t *)P2V(0x40E)) << 4;
    const rsdp_t *r = 0;

    if (ebda >= 0x80000 && ebda < 0xA0000)
        r = scan_rsdp(ebda, ebda + 1024);

    return r ? r : scan_rsdp(0xE0000, 0x100000);
}

/* Reads one AML integer (ZeroOp, OneOp, BytePrefix, WordPrefix). Returns bytes used. */
static int aml_integer(const uint8_t *p, const uint8_t *end, uint16_t *out)
{
    if (p >= end)
        return 0;

    switch (p[0])
    {
        case 0x00: *out = 0; return 1;
        case 0x01: *out = 1; return 1;
        case 0xFF: *out = 0xFF; return 1;
        case 0x0A:
            if (p + 1 >= end)
                return 0;
            *out = p[1];
            return 2;
        case 0x0B:
            if (p + 2 >= end)
                return 0;
            *out = (uint16_t)(p[1] | (p[2] << 8));
            return 3;
        default:
            /* A bare byte, as some firmware writes it. */
            *out = p[0];
            return 1;
    }
}

/*
 * Finds Name(_S5_, Package() { SLP_TYPa, SLP_TYPb, ... }) in the DSDT
 * without an AML interpreter: the bytes are
 *   08 ['\'] "_S5_" 12 PkgLength NumElements <integer> <integer> ...
 */
static int find_s5(const sdt_header_t *dsdt)
{
    const uint8_t *aml = (const uint8_t *)dsdt + sizeof(sdt_header_t);
    const uint8_t *end = (const uint8_t *)dsdt + dsdt->length;

    for (const uint8_t *p = aml; p + 4 < end; p++)
    {
        if (memcmp(p, "_S5_", 4) != 0)
            continue;

        int named = (p - aml >= 1 && p[-1] == 0x08) ||
                    (p - aml >= 2 && p[-1] == '\\' && p[-2] == 0x08);

        if (!named || p[4] != 0x12)
            continue;

        const uint8_t *q = p + 5;

        /* PkgLength: bits 6~7 of the lead byte count the extra bytes. */
        if (q >= end)
            return -1;

        q += 1 + ((q[0] >> 6) & 3);
        q++;                                /* NumElements */

        int n = aml_integer(q, end, &slp_typ_a);

        if (n == 0)
            return -1;

        if (aml_integer(q + n, end, &slp_typ_b) == 0)
            slp_typ_b = 0;

        return 0;
    }

    return -1;
}

void acpi_init(void)
{
    const rsdp_t *rsdp = find_rsdp();

    if (!rsdp)
    {
        klog("[acpi] no RSDP\n");
        return;
    }

    const sdt_header_t *rsdt = map_table(rsdp->rsdt_address);

    if (!rsdt || memcmp(rsdt->signature, "RSDT", 4) != 0)
    {
        klog("[acpi] bad RSDT\n");
        return;
    }

    const fadt_t *fadt = 0;
    uint32_t entries = (rsdt->length - sizeof(sdt_header_t)) / 4;
    const uint32_t *table = (const uint32_t *)(rsdt + 1);

    for (uint32_t i = 0; i < entries && !fadt; i++)
    {
        const sdt_header_t *h = map_table(table[i]);

        if (h && memcmp(h->signature, "FACP", 4) == 0 &&
            h->length >= sizeof(fadt_t))
            fadt = (const fadt_t *)h;
    }

    if (!fadt)
    {
        klog("[acpi] no FADT\n");
        return;
    }

    const sdt_header_t *dsdt = map_table(fadt->dsdt);

    if (!dsdt || memcmp(dsdt->signature, "DSDT", 4) != 0 || find_s5(dsdt) != 0)
    {
        klog("[acpi] no \\_S5_ in the DSDT\n");
        return;
    }

    smi_command = fadt->smi_command;
    acpi_enable_value = fadt->acpi_enable;
    pm1a_control = (uint16_t)fadt->pm1a_control_block;
    pm1b_control = (uint16_t)fadt->pm1b_control_block;

    acpi_ok = pm1a_control != 0;

    klog("[acpi] PM1a_CNT 0x%x PM1b_CNT 0x%x, S5 = %u/%u\n",
         pm1a_control, pm1b_control, slp_typ_a, slp_typ_b);
}

int acpi_available(void)
{
    return acpi_ok;
}

/* Switches from legacy to ACPI mode if the firmware has not done it. */
static void acpi_enable(void)
{
    if (inw(pm1a_control) & SCI_EN)
        return;

    if (!smi_command || !acpi_enable_value)
        return;

    outb((uint16_t)smi_command, acpi_enable_value);

    for (int i = 0; i < 1000000 && !(inw(pm1a_control) & SCI_EN); i++)
        ;
}

void acpi_poweroff(void)
{
    uint32_t flags = irq_save();

    if (acpi_ok)
    {
        acpi_enable();

        outw(pm1a_control, (uint16_t)((slp_typ_a << 10) | SLP_EN));

        if (pm1b_control)
            outw(pm1b_control, (uint16_t)((slp_typ_b << 10) | SLP_EN));

        for (int i = 0; i < 1000000; i++)
            ;
    }

    /* Emulator shortcuts: QEMU (newer, older/Bochs) and VirtualBox. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    irq_restore(flags);
}
