/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * ToAruOS PCI Initialization
 */

#include <system.h>
#include <ata.h>
#include <logging.h>

ide_channel_regs_t ide_channels[2];
ide_device_t ide_devices[4];
uint8_t ide_buf[2048] = {0};
uint8_t ide_irq_invoked = 0;
uint8_t atapi_packet[12] = {0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void ide_detect(void) {
	
}

void ata_io_wait(uint16_t bus) {
	inportb(bus + ATA_REG_ALTSTATUS);
	inportb(bus + ATA_REG_ALTSTATUS);
	inportb(bus + ATA_REG_ALTSTATUS);
	inportb(bus + ATA_REG_ALTSTATUS);
}

int ata_wait(uint16_t bus, int advanced) {
	uint8_t status = 0;

	ata_io_wait(bus);

	while ((status = inportb(bus + ATA_REG_STATUS)) & ATA_SR_BSY);

	if (advanced) {
		status = inportb(bus + ATA_REG_STATUS);
		if (status   & ATA_SR_ERR)  return 1;
		if (status   & ATA_SR_DF)   return 1;
		if (!(status & ATA_SR_DRQ)) return 1;
	}

	return 0;
}

void ata_select(uint16_t bus) {
	outportb(bus + ATA_REG_HDDEVSEL, 0xA0);
}

void ata_wait_ready(uint16_t bus) {
	while (inportb(bus + ATA_REG_STATUS) & ATA_SR_BSY);
}

void ide_init(uint16_t bus) {

	debug_print(NOTICE, "initializing IDE device on bus %d", bus);

	outportb(bus + 1, 1);
	outportb(bus + 0x306, 0);

	ata_select(bus);
	ata_io_wait(bus);

	outportb(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	ata_io_wait(bus);

	int status = inportb(bus + ATA_REG_COMMAND);
	debug_print(INFO, "status = %x", status);

	ata_wait_ready(bus);

	ata_identify_t device;
	uint16_t * buf = (uint16_t *)&device;

	for (int i = 0; i < 256; ++i) {
		buf[i] = inports(bus);
	}

	uint8_t * ptr = (uint8_t *)&device.model;
	for (int i = 0; i < 39; i+=2) {
		uint8_t tmp = ptr[i+1];
		ptr[i+1] = ptr[i];
		ptr[i] = tmp;
	}

	debug_print(NOTICE, "ata device %s", device.model);
	debug_print(NOTICE, "sectors_48 = %d", (uint32_t)device.sectors_48);
	debug_print(NOTICE, "sectors_28 = %d", device.sectors_28);

	outportb(bus + ATA_REG_CONTROL, 0x02);
}

void ide_read_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	int errors = 0;
try_again:
	outportb(bus + ATA_REG_CONTROL, 0x02);

	ata_wait_ready(bus);

	outportb(bus + ATA_REG_HDDEVSEL,  0xe0 | slave << 4 | 
								 (lba & 0x0f000000) >> 24);
	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_SECCOUNT0, 1);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

	if (ata_wait(bus, 1)) {
		debug_print(WARNING, "Error during ATA read of lba block %d", lba);
		errors++;
		if (errors > 4) {
			debug_print(WARNING, "-- Too many errors trying to read this block. Bailing.");
			return;
		}
		goto try_again;
	}

	int size = 256;
	inportsm(bus,buf,size);
	ata_wait(bus, 0);
}

void ide_write_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	outportb(bus + ATA_REG_CONTROL, 0x02);

	ata_wait_ready(bus);

	outportb(bus + ATA_REG_HDDEVSEL,  0xe0 | slave << 4 | 
								 (lba & 0x0f000000) >> 24);
	ata_wait(bus, 0);
	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_SECCOUNT0, 0x01);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
	ata_wait(bus, 0);
	int size = 256;
	outportsm(bus,buf,size);
	outportb(bus + 0x07, ATA_CMD_CACHE_FLUSH);
	ata_wait(bus, 0);
}

int ide_cmp(uint32_t * ptr1, uint32_t * ptr2, size_t size) {
	assert(!(size % 4));
	uint32_t i = 0;
	while (i < size) {
		if (*ptr1 != *ptr2) return 1;
		ptr1++;
		ptr2++;
		i += 4;
	}
	return 0;
}

void ide_write_sector_retry(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	uint8_t * read_buf = malloc(512);
	IRQ_OFF;
	do {
		ide_write_sector(bus,slave,lba,buf);
		ide_read_sector(bus,slave,lba,read_buf);
	} while (ide_cmp((uint32_t*)buf,(uint32_t*)read_buf,512));
	free(read_buf);
	IRQ_RES;
}
