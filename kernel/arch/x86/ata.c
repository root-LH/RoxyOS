#include "../../include/ata.h"
#include "../../include/io.h"

#define ATA_PRIMARY_IO       0x1F0

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

#define ATA_SR_BSY           0x80
#define ATA_SR_DRQ           0x08

static void ata_delay(void)
{
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

static void ata_wait_busy(void)
{
    while (inb(ATA_STATUS) & ATA_SR_BSY)
        ;
}

#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20

static int ata_wait_drq(void)
{
    uint8_t status;

    while (1)
    {
        status = inb(ATA_STATUS);

        if (status & ATA_SR_ERR)
            return -1;

        if (status & ATA_SR_DF)
            return -1;

        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
            return 0;
    }
}

void ata_init(void)
{
}

int ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *buf = (uint16_t *)buffer;

    ata_wait_busy();

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

    outb(ATA_SECTOR_COUNT, 1);

    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);

    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_delay();

    if (ata_wait_drq() != 0)
        return -1;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return 0;
}

int ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *buf = (const uint16_t *)buffer;

    ata_wait_busy();

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

    outb(ATA_SECTOR_COUNT, 1);

    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_delay();

    if (ata_wait_drq() != 0)
        return -1;

    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, buf[i]);

    ata_wait_busy();

    return 0;
}