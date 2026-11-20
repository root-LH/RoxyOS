#include "../../include/ata.h"
#include "../../include/io.h"
#include "../../include/cpu.h"
#include "../../include/wait.h"
#include "../../include/process.h"
#include "../../include/printk.h"

#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CTRL     0x3F6

#define ATA_DATA             (ATA_PRIMARY_IO + 0)
#define ATA_ERROR            (ATA_PRIMARY_IO + 1)
#define ATA_SECTOR_COUNT     (ATA_PRIMARY_IO + 2)
#define ATA_LBA_LOW          (ATA_PRIMARY_IO + 3)
#define ATA_LBA_MID          (ATA_PRIMARY_IO + 4)
#define ATA_LBA_HIGH         (ATA_PRIMARY_IO + 5)
#define ATA_DRIVE            (ATA_PRIMARY_IO + 6)
#define ATA_STATUS           (ATA_PRIMARY_IO + 7)
#define ATA_COMMAND          (ATA_PRIMARY_IO + 7)

#define ATA_CMD_READ         0x20
#define ATA_CMD_WRITE        0x30
#define ATA_CMD_IDENTIFY     0xEC

#define ATA_SR_ERR           0x01
#define ATA_SR_DRQ           0x08
#define ATA_SR_DF            0x20
#define ATA_SR_BSY           0x80

/* Status polls before giving up (no disk, or a hung drive). */
#define ATA_POLL_LIMIT       1000000

/* 8259 masks: IRQ14 is line 6 on the slave PIC, cascaded through IRQ2. */
#define PIC1_DATA            0x21
#define PIC2_DATA            0xA1

/* One request at a time: the task file registers are shared. */
static kmutex_t ata_lock = KMUTEX_INIT;

/* IRQ14 wakes the process that issued the current command. */
static wait_queue_t ata_wq = WAIT_QUEUE_INIT;
static volatile int ata_irq_fired = 0;

static void ata_delay(void)
{
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

static int ata_wait_busy(void)
{
    for (int i = 0; i < ATA_POLL_LIMIT; i++)
    {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY))
            return 0;
    }

    return -1;
}

static int ata_wait_drq(void)
{
    for (int i = 0; i < ATA_POLL_LIMIT; i++)
    {
        uint8_t status = inb(ATA_STATUS);

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return -1;

        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
            return 0;
    }

    return -1;
}

/*
 * Waits for the drive to finish the current step. With a process to put
 * to sleep, the CPU runs something else until IRQ14; during early boot
 * the status register is polled instead. Interrupts must be disabled.
 */
static void ata_wait_irq(void)
{
    if (!process_current())
        return;

    while (!ata_irq_fired)
        wait_queue_sleep(&ata_wq, 0);
}

void ata_irq_handler(void)
{
    /* Reading the status register acknowledges the interrupt. */
    inb(ATA_STATUS);

    ata_irq_fired = 1;
    wait_queue_wake_all(&ata_wq);
}

/* LBA28 sectors reported by IDENTIFY DEVICE (0 = no drive). */
static uint32_t ata_sectors = 0;

uint32_t ata_sector_count(void)
{
    return ata_sectors;
}

/* IDENTIFY DEVICE, polled (runs at boot, before the scheduler). */
static void ata_identify(void)
{
    uint16_t id[256];

    outb(ATA_DRIVE, 0xA0);
    ata_delay();

    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* Status 0: no drive on the bus. */
    if (inb(ATA_STATUS) == 0 || ata_wait_drq() != 0)
        return;

    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);

    /* Words 60-61: total user-addressable sectors (LBA28). */
    ata_sectors = id[60] | ((uint32_t)id[61] << 16);
}

void ata_init(void)
{
    /* nIEN = 0: let the drive raise IRQ14. */
    outb(ATA_PRIMARY_CTRL, 0x00);

    outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << 2));
    outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << 6));

    ata_identify();

    klog("[ata] primary master: %u sectors (%u KB), IRQ14 enabled\n",
         ata_sectors, ata_sectors / 2);
}

/* Selects lba and issues cmd for one sector. Interrupts must be disabled. */
static int ata_issue(uint32_t lba, uint8_t cmd)
{
    if (ata_wait_busy() != 0)
        return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

    outb(ATA_SECTOR_COUNT, 1);

    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);

    ata_irq_fired = 0;

    outb(ATA_COMMAND, cmd);

    ata_delay();

    return 0;
}

int ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *buf = (uint16_t *)buffer;
    int result = -1;

    kmutex_lock(&ata_lock);
    uint32_t flags = irq_save();

    if (ata_issue(lba, ATA_CMD_READ) != 0)
        goto out;

    /* The drive raises IRQ14 once the sector is in its buffer. */
    ata_wait_irq();

    if (ata_wait_drq() != 0)
        goto out;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    result = 0;

out:
    irq_restore(flags);
    kmutex_unlock(&ata_lock);

    if (result != 0)
        klog("[ata] read of LBA %u failed\n", lba);

    return result;
}

int ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *buf = (const uint16_t *)buffer;
    int result = -1;

    kmutex_lock(&ata_lock);
    uint32_t flags = irq_save();

    if (ata_issue(lba, ATA_CMD_WRITE) != 0)
        goto out;

    /* PIO write: no IRQ before the data, the drive just asks for it (DRQ). */
    if (ata_wait_drq() != 0)
        goto out;

    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, buf[i]);

    /* IRQ14 again when the sector has been written. */
    ata_wait_irq();

    if (ata_wait_busy() != 0)
        goto out;

    result = (inb(ATA_STATUS) & (ATA_SR_ERR | ATA_SR_DF)) ? -1 : 0;

out:
    irq_restore(flags);
    kmutex_unlock(&ata_lock);

    if (result != 0)
        klog("[ata] write of LBA %u failed\n", lba);

    return result;
}
