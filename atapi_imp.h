#pragma once

static struct ata_device ata_primary_master   = {.io_base = 0x1F0, .control = 0x3F6, .slave = 0};
static struct ata_device ata_primary_slave    = {.io_base = 0x1F0, .control = 0x3F6, .slave = 1};
static struct ata_device ata_secondary_master = {.io_base = 0x170, .control = 0x376, .slave = 0};
static struct ata_device ata_secondary_slave  = {.io_base = 0x170, .control = 0x376, .slave = 1};

static void ata_io_wait(struct ata_device * dev) {
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
}

static int ata_status_wait(struct ata_device * dev, int timeout) {
	int status;
	if (timeout > 0) {
		int i = 0;
		while ((status = inportb(dev->io_base + ATA_REG_STATUS)) & ATA_SR_BSY && (i < timeout)) i++;
	} else {
		while ((status = inportb(dev->io_base + ATA_REG_STATUS)) & ATA_SR_BSY);
	}
	return status;
}

static void ata_soft_reset(struct ata_device * dev) {
	outportb(dev->control, 0x04);
	ata_io_wait(dev);
	outportb(dev->control, 0x00);
}

static int ata_wait(struct ata_device * dev, int advanced) {
	uint8_t status = 0;

	ata_io_wait(dev);

	status = ata_status_wait(dev, -1);

	if (advanced) {
		status = inportb(dev->io_base + ATA_REG_STATUS);
		if (status   & ATA_SR_ERR)  return 1;
		if (status   & ATA_SR_DF)   return 1;
		if (!(status & ATA_SR_DRQ)) return 1;
	}

	return 0;
}

static void atapi_device_init(struct ata_device * dev) {

	dev->is_atapi = 1;

	outportb(dev->io_base + 1, 1);
	outportb(dev->control, 0);

	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	outportb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
	ata_io_wait(dev);

	ata_wait(dev, 0);

	uint16_t * buf = (uint16_t *)&dev->identity;

	for (int i = 0; i < 256; ++i) {
		buf[i] = inports(dev->io_base);
	}

	uint8_t * ptr = (uint8_t *)&dev->identity.model;
	for (int i = 0; i < 39; i+=2) {
		uint8_t tmp = ptr[i+1];
		ptr[i+1] = ptr[i];
		ptr[i] = tmp;
	}

	/* Detect medium */
	atapi_command_t command;
	memset(&command, 0, sizeof(command));
	command.command_bytes[0] = 0x25;

	uint16_t bus = dev->io_base;

	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_LBA1, 0x08);
	outportb(bus + ATA_REG_LBA2, 0x08);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_PACKET);

	/* poll */
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
	}

	for (int i = 0; i < 6; ++i) {
		outports(bus, command.command_words[i]);
	}

	/* poll */
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_read;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
		if ((status & ATA_SR_DRQ)) break;
	}

	uint16_t data[4];

	for (int i = 0; i < 4; ++i) {
		data[i] = inports(bus);
	}

#define htonl(l)  ( (((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | (((l) & 0xFF000000) >> 24))
	uint32_t lba, blocks;;
	memcpy(&lba, &data[0], sizeof(uint32_t));
	lba = htonl(lba);
	memcpy(&blocks, &data[2], sizeof(uint32_t));
	blocks = htonl(blocks);

	dev->atapi_lba = lba;
	dev->atapi_sector_size = blocks;

	return;

atapi_error_read:
	return;

atapi_error:
	return;
}

static int ata_device_detect(struct ata_device * dev) {
	ata_soft_reset(dev);
	ata_io_wait(dev);
	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);
	ata_status_wait(dev, 10000);

	unsigned char cl = inportb(dev->io_base + ATA_REG_LBA1); /* CYL_LO */
	unsigned char ch = inportb(dev->io_base + ATA_REG_LBA2); /* CYL_HI */

	if (cl == 0xFF && ch == 0xFF) {
		/* Nothing here */
		return 0;
	}
	if ((cl == 0x00 && ch == 0x00) ||
	    (cl == 0x3C && ch == 0xC3)) {
		return 1;
	} else if ((cl == 0x14 && ch == 0xEB) ||
	           (cl == 0x69 && ch == 0x96)) {
		atapi_device_init(dev);
		return 2;
	}

	return 0;
}

static int _read_12 = 1;

static void ata_device_read_sector_atapi(struct ata_device * dev, uint32_t lba, uint8_t * buf) {

	if (!dev->is_atapi) return;


	uint16_t bus = dev->io_base;

_try_again:
	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_LBA1, dev->atapi_sector_size & 0xFF);
	outportb(bus + ATA_REG_LBA2, dev->atapi_sector_size >> 8);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_PACKET);

	/* poll */
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_on_read_setup;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
	}

	atapi_command_t command;
	command.command_bytes[0] = _read_12 ? 0xA8 : 0x28;
	command.command_bytes[1] = 0;
	command.command_bytes[2] = (lba >> 0x18) & 0xFF;
	command.command_bytes[3] = (lba >> 0x10) & 0xFF;
	command.command_bytes[4] = (lba >> 0x08) & 0xFF;
	command.command_bytes[5] = (lba >> 0x00) & 0xFF;
	command.command_bytes[6] = 0;
	command.command_bytes[7] = 0;
	command.command_bytes[8] = _read_12 ? 0 : 1; /* bit 0 = PMI (0, last sector) */
	command.command_bytes[9] = _read_12 ? 1 : 0; /* control */
	command.command_bytes[10] = 0;
	command.command_bytes[11] = 0;

	for (int i = 0; i < 6; ++i) {
		outports(bus, command.command_words[i]);
	}

	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_on_read_setup_cmd;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
	}

	uint16_t size_to_read = inportb(bus + ATA_REG_LBA2) << 8;
	size_to_read = size_to_read | inportb(bus + ATA_REG_LBA1);

	inportsm(bus,buf,size_to_read/2);

	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_on_read_setup;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
	}

	return;

atapi_error_on_read_setup:
	print("error on setup\n");
	return;
atapi_error_on_read_setup_cmd:
	if (_read_12) {
		_read_12 = 0;
		print("trying again\n");
		goto _try_again;
	}
	print("error on cmd\n");
	return;
}


