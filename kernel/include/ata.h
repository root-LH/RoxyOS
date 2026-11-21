#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

void ata_init(void);

/* Size of the primary master from IDENTIFY (0 if there is no drive). */
uint32_t ata_sector_count(void);

/* IRQ14 */
void ata_irq_handler(void);

/* Sectors one READ/WRITE SECTORS command can move (count register 0 = 256). */
#define ATA_MAX_SECTORS 256

/*
 * PIO transfer of count (1..ATA_MAX_SECTORS) consecutive sectors with one
 * command. The caller sleeps on IRQ14 between sectors (polling before the
 * scheduler starts); if the IRQ does not come within a few seconds the
 * drive is checked and reset when hung. Returns 0, or -1 on a drive
 * error, timeout, or a range outside the disk.
 */
int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer);
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer);

/* One sector. */
int ata_read_sector(uint32_t lba, void *buffer);
int ata_write_sector(uint32_t lba, const void *buffer);

#endif