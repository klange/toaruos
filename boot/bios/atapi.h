#pragma once
#include <stdint.h>
#include "types.h"
#include "ata.h"

extern int ata_device_detect(struct ata_device * dev);
extern struct ata_device ata_primary_master;
extern struct ata_device ata_primary_slave;
extern struct ata_device ata_secondary_master;
extern struct ata_device ata_secondary_slave;
extern void ata_device_read_sectors_atapi(struct ata_device * dev, uint32_t lba, uint8_t * buf, int sectors);
#define ata_device_read_sector_atapi(a,b,c) ata_device_read_sectors_atapi(a,b,c,1)
