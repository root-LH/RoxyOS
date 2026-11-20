#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

void ata_init(void);

/* IRQ14 */
void ata_irq_handler(void);

/*
 * One sector, PIO. The caller sleeps until the drive's IRQ (polling before
 * the scheduler starts). Returns 0, or -1 on a drive error or timeout.
 */

int ata_read_sector(uint32_t lba, void *buffer);
int ata_write_sector(uint32_t lba, const void *buffer);

#endif