#include <system.h>
#include <logging.h>
#include <ata.h>

#define SECTORSIZE		512
#define DISK_PORT		0x1F0

mbr_t mbr;

int read_partition_map(int device) {

	ide_init(DISK_PORT);

	ide_read_sector(DISK_PORT, 0, 0, (uint8_t *)&mbr);

	if (mbr.signature[0] == 0x55 && mbr.signature[1] == 0xAA) {
		debug_print(INFO, "Partition table found.");

		for (int i = 0; i < 4; ++i) {
			if (mbr.partitions[i].status & 0x80) {
				debug_print(NOTICE, "Partition #%d: @%d+%d", i+1, mbr.partitions[i].lba_first_sector, mbr.partitions[i].sector_count);
			} else {
				debug_print(NOTICE, "Partition #%d: inactive", i+1);
			}
		}

		return 0;
	} else {
		debug_print(ERROR, "Did not find partition table.");
		debug_print(ERROR, "Signature was 0x%x 0x%x instead of 0x55 0xAA", mbr.signature[0], mbr.signature[1]);

		debug_print(ERROR, "Parsing anyone yields:");

		for (int i = 0; i < 4; ++i) {
			if (mbr.partitions[i].status & 0x80) {
				debug_print(NOTICE, "Partition #%d: @%d+%d", i+1, mbr.partitions[i].lba_first_sector, mbr.partitions[i].sector_count);
			} else {
				debug_print(NOTICE, "Partition #%d: inactive", i+1);
			}
		}


	}

	return 1;
}
