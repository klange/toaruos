/**
 * @brief (P)ATA / IDE disk driver
 * @file modules/ata.c
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/syscall.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/list.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/irq.h>

#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DSC     0x10
#define ATA_SR_DRQ     0x08
#define ATA_SR_CORR    0x04
#define ATA_SR_IDX     0x02
#define ATA_SR_ERR     0x01

#define ATA_ER_BBK      0x80
#define ATA_ER_UNC      0x40
#define ATA_ER_MC       0x20
#define ATA_ER_IDNF     0x10
#define ATA_ER_MCR      0x08
#define ATA_ER_ABRT     0x04
#define ATA_ER_TK0NF    0x02
#define ATA_ER_AMNF     0x01

#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_READ_PIO_EXT      0x24
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_WRITE_PIO_EXT     0x34
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_CACHE_FLUSH_EXT   0xEA
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_PACKET   0xA1
#define ATA_CMD_IDENTIFY          0xEC

#define ATAPI_CMD_READ       0xA8
#define ATAPI_CMD_EJECT      0x1B

#define ATA_IDENT_DEVICETYPE   0
#define ATA_IDENT_CYLINDERS    2
#define ATA_IDENT_HEADS        6
#define ATA_IDENT_SECTORS      12
#define ATA_IDENT_SERIAL       20
#define ATA_IDENT_MODEL        54
#define ATA_IDENT_CAPABILITIES 98
#define ATA_IDENT_FIELDVALID   106
#define ATA_IDENT_MAX_LBA      120
#define ATA_IDENT_COMMANDSETS  164
#define ATA_IDENT_MAX_LBA_EXT  200

#define IDE_ATA        0x00
#define IDE_ATAPI      0x01
 
#define ATA_MASTER     0x00
#define ATA_SLAVE      0x01

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07
#define ATA_REG_SECCOUNT1  0x08
#define ATA_REG_LBA3       0x09
#define ATA_REG_LBA4       0x0A
#define ATA_REG_LBA5       0x0B
#define ATA_REG_CONTROL    0x0C
#define ATA_REG_ALTSTATUS  0x0C
#define ATA_REG_DEVADDRESS 0x0D

// Channels:
#define ATA_PRIMARY      0x00
#define ATA_SECONDARY    0x01

// Directions:
#define ATA_READ      0x00
#define ATA_WRITE     0x01

typedef struct {
	uint16_t base;
	uint16_t ctrl;
	uint16_t bmide;
	uint16_t nien;
} ide_channel_regs_t;

typedef struct {
	uint8_t  reserved;
	uint8_t  channel;
	uint8_t  drive;
	uint16_t type;
	uint16_t signature;
	uint16_t capabilities;
	uint32_t command_sets;
	uint32_t size;
	uint8_t  model[41];
} ide_device_t;

typedef struct {
	uint16_t flags;
	uint16_t unused1[9];
	char     serial[20];
	uint16_t unused2[3];
	char     firmware[8];
	char     model[40];
	uint16_t sectors_per_int;
	uint16_t unused3;
	uint16_t capabilities[2];
	uint16_t unused4[2];
	uint16_t valid_ext_data;
	uint16_t unused5[5];
	uint16_t size_of_rw_mult;
	uint32_t sectors_28;
	uint16_t unused6[38];
	uint64_t sectors_48;
	uint16_t unused7[152];
} __attribute__((packed)) ata_identify_t;

static char ata_drive_char = 'a';
static int  cdrom_number = 0;
static uint32_t ata_pci = 0x00000000;
static list_t * atapi_waiter;
static int atapi_in_progress = 0;

typedef union {
	uint8_t command_bytes[12];
	uint16_t command_words[6];
} atapi_command_t;

/* 8086:7010 */
static void find_ata_pci(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if ((vendorid == 0x8086) && (deviceid == 0x7010 || deviceid == 0x7111)) {
		*((uint32_t *)extra) = device;
	}
}

typedef struct {
	uintptr_t offset;
	uint16_t bytes;
	uint16_t last;
} prdt_t;

struct ata_device {
	int io_base;
	int control;
	int slave;
	int is_atapi;
	ata_identify_t identity;
	prdt_t * dma_prdt;
	uintptr_t dma_prdt_phys;
	uint8_t * dma_start;
	uintptr_t dma_start_phys;
	uint32_t bar4;
	uint32_t atapi_lba;
	uint32_t atapi_sector_size;
};

static struct ata_device ata_primary_master   = {.io_base = 0x1F0, .control = 0x3F6, .slave = 0};
static struct ata_device ata_primary_slave    = {.io_base = 0x1F0, .control = 0x3F6, .slave = 1};
static struct ata_device ata_secondary_master = {.io_base = 0x170, .control = 0x376, .slave = 0};
static struct ata_device ata_secondary_slave  = {.io_base = 0x170, .control = 0x376, .slave = 1};

//static volatile uint8_t ata_lock = 0;
static spin_lock_t ata_lock = { 0 };

/* TODO support other sector sizes */
#define ATA_SECTOR_SIZE 512

static void ata_device_read_sector(struct ata_device * dev, uint64_t lba, uint8_t * buf);
static void ata_device_read_sector_atapi(struct ata_device * dev, uint64_t lba, uint8_t * buf);
static void ata_device_write_sector_retry(struct ata_device * dev, uint64_t lba, uint8_t * buf);

static off_t ata_max_offset(struct ata_device * dev) {
	uint64_t sectors = dev->identity.sectors_48;
	if (!sectors) {
		/* Fall back to sectors_28 */
		sectors = dev->identity.sectors_28;
	}

	return sectors * ATA_SECTOR_SIZE;
}

static off_t atapi_max_offset(struct ata_device * dev) {
	uint64_t max_sector = dev->atapi_lba;

	if (!max_sector) return 0;

	return (max_sector + 1) * dev->atapi_sector_size;
}

static ssize_t read_ata(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct ata_device * dev = (struct ata_device *)node->device;
	unsigned int start_block = offset / ATA_SECTOR_SIZE;
	unsigned int end_block = (offset + size - 1) / ATA_SECTOR_SIZE;
	unsigned int x_offset = 0;


	if (offset > ata_max_offset(dev)) {
		return 0;
	}

	if (offset + (ssize_t)size > ata_max_offset(dev)) {
		unsigned int i = ata_max_offset(dev) - offset;
		size = i;
	}

	if (offset % ATA_SECTOR_SIZE || size < ATA_SECTOR_SIZE) {
		unsigned int prefix_size = (ATA_SECTOR_SIZE - (offset % ATA_SECTOR_SIZE));
		if (prefix_size > size) prefix_size = size;
		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, start_block, (uint8_t *)tmp);

		memcpy(buffer, (void *)((uintptr_t)tmp + ((uintptr_t)offset % ATA_SECTOR_SIZE)), prefix_size);

		free(tmp);

		x_offset += prefix_size;
		start_block++;
	}

	if ((offset + size)  % ATA_SECTOR_SIZE && start_block <= end_block) {
		unsigned int postfix_size = (offset + size) % ATA_SECTOR_SIZE;
		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, end_block, (uint8_t *)tmp);

		memcpy((void *)((uintptr_t)buffer + size - postfix_size), tmp, postfix_size);

		free(tmp);

		end_block--;
	}

	while (start_block <= end_block) {
		ata_device_read_sector(dev, start_block, (uint8_t *)((uintptr_t)buffer + x_offset));
		x_offset += ATA_SECTOR_SIZE;
		start_block++;
	}

	return size;
}

static ssize_t read_atapi(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {

	struct ata_device * dev = (struct ata_device *)node->device;

	unsigned int start_block = offset / dev->atapi_sector_size;
	unsigned int end_block = (offset + size - 1) / dev->atapi_sector_size;

	unsigned int x_offset = 0;

	if (offset > atapi_max_offset(dev)) {
		return 0;
	}

	if (offset + (ssize_t)size > atapi_max_offset(dev)) {
		unsigned int i = atapi_max_offset(dev) - offset;
		size = i;
	}

	if (offset % dev->atapi_sector_size || size < dev->atapi_sector_size) {
		unsigned int prefix_size = (dev->atapi_sector_size - (offset % dev->atapi_sector_size));
		if (prefix_size > size) prefix_size = size;
		char * tmp = malloc(dev->atapi_sector_size);
		ata_device_read_sector_atapi(dev, start_block, (uint8_t *)tmp);

		memcpy(buffer, (void *)((uintptr_t)tmp + ((uintptr_t)offset % dev->atapi_sector_size)), prefix_size);

		free(tmp);

		x_offset += prefix_size;
		start_block++;
	}

	if ((offset + size)  % dev->atapi_sector_size && start_block <= end_block) {
		unsigned int postfix_size = (offset + size) % dev->atapi_sector_size;
		char * tmp = malloc(dev->atapi_sector_size);
		ata_device_read_sector_atapi(dev, end_block, (uint8_t *)tmp);

		memcpy((void *)((uintptr_t)buffer + size - postfix_size), tmp, postfix_size);

		free(tmp);

		end_block--;
	}

	while (start_block <= end_block) {
		ata_device_read_sector_atapi(dev, start_block, (uint8_t *)((uintptr_t)buffer + x_offset));
		x_offset += dev->atapi_sector_size;
		start_block++;
	}

	return size;
}


static ssize_t write_ata(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct ata_device * dev = (struct ata_device *)node->device;

	unsigned int start_block = offset / ATA_SECTOR_SIZE;
	unsigned int end_block = (offset + size - 1) / ATA_SECTOR_SIZE;

	unsigned int x_offset = 0;

	if (offset > ata_max_offset(dev)) {
		return 0;
	}

	if (offset + (ssize_t)size > ata_max_offset(dev)) {
		unsigned int i = ata_max_offset(dev) - offset;
		size = i;
	}

	if (offset % ATA_SECTOR_SIZE) {
		unsigned int prefix_size = (ATA_SECTOR_SIZE - (offset % ATA_SECTOR_SIZE));

		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, start_block, (uint8_t *)tmp);

		memcpy((void *)((uintptr_t)tmp + ((uintptr_t)offset % ATA_SECTOR_SIZE)), buffer, prefix_size);
		ata_device_write_sector_retry(dev, start_block, (uint8_t *)tmp);

		free(tmp);
		x_offset += prefix_size;
		start_block++;
	}

	if ((offset + size)  % ATA_SECTOR_SIZE && start_block <= end_block) {
		unsigned int postfix_size = (offset + size) % ATA_SECTOR_SIZE;

		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, end_block, (uint8_t *)tmp);

		memcpy(tmp, (void *)((uintptr_t)buffer + size - postfix_size), postfix_size);

		ata_device_write_sector_retry(dev, end_block, (uint8_t *)tmp);

		free(tmp);
		end_block--;
	}

	while (start_block <= end_block) {
		ata_device_write_sector_retry(dev, start_block, (uint8_t *)((uintptr_t)buffer + x_offset));
		x_offset += ATA_SECTOR_SIZE;
		start_block++;
	}

	return size;
}

static void open_ata(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_ata(fs_node_t * node) {
	return;
}

static fs_node_t * atapi_device_create(struct ata_device * device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	snprintf(fnode->name, 20, "cdrom%d", cdrom_number);
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0664;
	fnode->length  = atapi_max_offset(device);
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_atapi;
	fnode->write   = NULL; /* no write support */
	fnode->open    = open_ata;
	fnode->close   = close_ata;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL; /* TODO, identify, etc? */
	return fnode;
}


static fs_node_t * ata_device_create(struct ata_device * device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	snprintf(fnode->name, 10, "atadev%d", ata_drive_char - 'a');
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0660;
	fnode->length  = ata_max_offset(device); /* TODO */
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_ata;
	fnode->write   = write_ata;
	fnode->open    = open_ata;
	fnode->close   = close_ata;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL; /* TODO, identify, etc? */
	return fnode;
}

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

static void ata_soft_reset(struct ata_device * dev) {
	outportb(dev->control, 0x04);
	ata_io_wait(dev);
	outportb(dev->control, 0x00);
}

static int ata_irq_handler(struct regs *r) {
	inportb(ata_primary_master.io_base + ATA_REG_STATUS);
	if (atapi_in_progress) {
		wakeup_queue(atapi_waiter);
	}
	irq_ack(14);
	return 1;
}

static int ata_irq_handler_s(struct regs *r) {
	inportb(ata_secondary_master.io_base + ATA_REG_STATUS);
	if (atapi_in_progress) {
		wakeup_queue(atapi_waiter);
	}
	irq_ack(15);
	return 1;
}

static void * kvmalloc_p(size_t size, uintptr_t * outphys) {
	uintptr_t index = mmu_allocate_n_frames(size / 0x1000) << 12;
	*outphys = index;
	return mmu_map_from_physical(index);
}

static void ata_device_init(struct ata_device * dev) {
	outportb(dev->io_base + 1, 1);
	outportb(dev->control, 0);

	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	outportb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	ata_io_wait(dev);

	inportb(dev->io_base + ATA_REG_COMMAND);

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

	dev->is_atapi = 0;
	dev->dma_prdt  = (void *)kvmalloc_p(sizeof(prdt_t) * 1, &dev->dma_prdt_phys);
	dev->dma_start = (void *)kvmalloc_p(4096, &dev->dma_start_phys);
	dev->dma_prdt[0].offset = dev->dma_start_phys;
	dev->dma_prdt[0].bytes = 512;
	dev->dma_prdt[0].last = 0x8000;

	uint16_t command_reg = pci_read_field(ata_pci, PCI_COMMAND, 4);
	if (!(command_reg & (1 << 2))) {
		command_reg |= (1 << 2); /* bit 2 */
		pci_write_field(ata_pci, PCI_COMMAND, 4, command_reg);
		command_reg = pci_read_field(ata_pci, PCI_COMMAND, 4);
	}

	dev->bar4 = pci_read_field(ata_pci, PCI_BAR4, 4);

	if (dev->bar4 & 0x00000001) {
		dev->bar4 = dev->bar4 & 0xFFFFFFFC;
	} else {
		return; /* No DMA because we're not sure what to do here */
	}
}

static int atapi_device_init(struct ata_device * dev) {

	dev->is_atapi = 1;

	outportb(dev->io_base + 1, 1);
	outportb(dev->control, 0);

	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	outportb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
	ata_io_wait(dev);

	inportb(dev->io_base + ATA_REG_COMMAND);

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
	command.command_bytes[0] = 0x25;
	command.command_bytes[1] = 0;
	command.command_bytes[2] = 0;
	command.command_bytes[3] = 0;
	command.command_bytes[4] = 0;
	command.command_bytes[5] = 0;
	command.command_bytes[6] = 0;
	command.command_bytes[7] = 0;
	command.command_bytes[8] = 0; /* bit 0 = PMI (0, last sector) */
	command.command_bytes[9] = 0; /* control */
	command.command_bytes[10] = 0;
	command.command_bytes[11] = 0;

	uint16_t bus = dev->io_base;

	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_LBA1, 0x08);
	outportb(bus + ATA_REG_LBA2, 0x08);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_PACKET);

	/* poll */
	int timeout = 100;
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error;
		if (timeout-- < 100) goto atapi_timeout;
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
	}

	for (int i = 0; i < 6; ++i) {
		outports(bus, command.command_words[i]);
	}

	/* poll */
	timeout = 100;
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_read;
		if (timeout-- < 100) goto atapi_timeout;
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

	if (!lba) return 1;

	return 0;

atapi_error_read:
	return 1;

atapi_error:
	return 1;

atapi_timeout:
	return 1;
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
		/* Parallel ATA device, or emulated SATA */

		char devname[64];
		snprintf((char *)&devname, 20, "/dev/hd%c", ata_drive_char);
		fs_node_t * node = ata_device_create(dev);
		vfs_mount(devname, node);
		ata_device_init(dev);
		node->length  = ata_max_offset(dev);

		ata_drive_char++;
		return 1;
	} else if ((cl == 0x14 && ch == 0xEB) ||
	           (cl == 0x69 && ch == 0x96)) {

		char devname[64];
		snprintf((char *)&devname, 20, "/dev/cdrom%d", cdrom_number);

		if (atapi_device_init(dev)) {
			return 0;
		}
		fs_node_t * node = atapi_device_create(dev);
		vfs_mount(devname, node);

		cdrom_number++;

		return 2;
	}

	/* TODO: ATAPI, SATA, SATAPI */
	return 0;
}

static void ata_device_read_sector(struct ata_device * dev, uint64_t lba, uint8_t * buf) {
	uint16_t bus = dev->io_base;
	uint8_t slave = dev->slave;

	if (dev->is_atapi) return;

	spin_lock(ata_lock);

	ata_wait(dev, 0);

	/* Stop */
	outportb(dev->bar4, 0x00);

	/* Set the PRDT */
	outportl(dev->bar4 + 0x04, dev->dma_prdt_phys);

	/* Enable error, irq status */
	outportb(dev->bar4 + 0x2, inportb(dev->bar4 + 0x02) | 0x04 | 0x02);

	/* set read */
	outportb(dev->bar4, 0x08);

	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if (!(status & ATA_SR_BSY)) break;
	}

	outportb(bus + ATA_REG_CONTROL, 0x00);
	outportb(bus + ATA_REG_HDDEVSEL, 0xe0 | slave << 4);
	ata_io_wait(dev);
	outportb(bus + ATA_REG_FEATURES, 0x00);

	outportb(bus + ATA_REG_SECCOUNT0, 0);
	outportb(bus + ATA_REG_LBA0, (lba & 0xff000000) >> 24);
	outportb(bus + ATA_REG_LBA1, (lba & 0xff00000000) >> 32);
	outportb(bus + ATA_REG_LBA2, (lba & 0xff0000000000) >> 40);

	outportb(bus + ATA_REG_SECCOUNT0, 1);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);

	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
	}
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_READ_DMA_EXT);

	ata_io_wait(dev);

	outportb(dev->bar4, 0x08 | 0x01);

	while (1) {
		int status = inportb(dev->bar4 + 0x02);
		int dstatus = inportb(dev->io_base + ATA_REG_STATUS);
		if (!(status & 0x04)) {
			continue;
		}
		if (!(dstatus & ATA_SR_BSY)) {
			break;
		}
	}

	/* Copy from DMA buffer to output buffer. */
	memcpy(buf, dev->dma_start, 512);

	/* Inform device we are done. */
	outportb(dev->bar4 + 0x2, inportb(dev->bar4 + 0x02) | 0x04 | 0x02);

	spin_unlock(ata_lock);
}

static void ata_device_read_sector_atapi(struct ata_device * dev, uint64_t lba, uint8_t * buf) {

	if (!dev->is_atapi) return;

	uint16_t bus = dev->io_base;
	spin_lock(ata_lock);

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

	atapi_in_progress = 1;


	atapi_command_t command;
	command.command_bytes[0] = 0xA8;
	command.command_bytes[1] = 0;
	command.command_bytes[2] = (lba >> 0x18) & 0xFF;
	command.command_bytes[3] = (lba >> 0x10) & 0xFF;
	command.command_bytes[4] = (lba >> 0x08) & 0xFF;
	command.command_bytes[5] = (lba >> 0x00) & 0xFF;
	command.command_bytes[6] = 0;
	command.command_bytes[7] = 0;
	command.command_bytes[8] = 0; /* bit 0 = PMI (0, last sector) */
	command.command_bytes[9] = 1; /* control */
	command.command_bytes[10] = 0;
	command.command_bytes[11] = 0;

	for (int i = 0; i < 6; ++i) {
		outports(bus, command.command_words[i]);
	}

	/* Wait */
	sleep_on(atapi_waiter);

	atapi_in_progress = 0;

	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if ((status & ATA_SR_ERR)) goto atapi_error_on_read_setup;
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

atapi_error_on_read_setup:
	spin_unlock(ata_lock);

}

static void ata_device_write_sector(struct ata_device * dev, uint64_t lba, uint8_t * buf) {
	uint16_t bus = dev->io_base;
	uint8_t slave = dev->slave;

	spin_lock(ata_lock);

	outportb(bus + ATA_REG_CONTROL, 0x02);

	ata_wait(dev, 0);
	outportb(bus + ATA_REG_HDDEVSEL, 0xe0 | slave << 4);
	ata_wait(dev, 0);

	outportb(bus + ATA_REG_FEATURES, 0x00);

	outportb(bus + ATA_REG_SECCOUNT0, 0);
	outportb(bus + ATA_REG_LBA0, (lba & 0xff000000) >> 24);
	outportb(bus + ATA_REG_LBA1, (lba & 0xff00000000) >> 32);
	outportb(bus + ATA_REG_LBA2, (lba & 0xff0000000000) >> 40);

	outportb(bus + ATA_REG_SECCOUNT0, 1);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);

	outportb(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
	ata_wait(dev, 0);
	int size = ATA_SECTOR_SIZE / 2;
	outportsm(bus,buf,size);
	outportb(bus + 0x07, ATA_CMD_CACHE_FLUSH);
	ata_wait(dev, 0);
	spin_unlock(ata_lock);
}

static int buffer_compare(uint32_t * ptr1, uint32_t * ptr2, size_t size) {
	size_t i = 0;
	while (i < size) {
		if (*ptr1 != *ptr2) return 1;
		ptr1++;
		ptr2++;
		i += sizeof(uint32_t);
	}
	return 0;
}

static void ata_device_write_sector_retry(struct ata_device * dev, uint64_t lba, uint8_t * buf) {
	uint64_t sectors = dev->identity.sectors_48;
	if (lba >= sectors) return;
	uint8_t * read_buf = malloc(ATA_SECTOR_SIZE);
	do {
		ata_device_write_sector(dev, lba, buf);
		ata_device_read_sector(dev, lba, read_buf);
	} while (buffer_compare((uint32_t *)buf, (uint32_t *)read_buf, ATA_SECTOR_SIZE));
	free(read_buf);
}

static int ata_initialize(int argc, char * argv[]) {
	/* Detect drives and mount them */

	/* Locate ATA device via PCI */
	pci_scan(&find_ata_pci, -1, &ata_pci);

	irq_install_handler(14, ata_irq_handler, "ide master");
	irq_install_handler(15, ata_irq_handler_s, "ide slave");

	atapi_waiter = list_create("atapi waiter", NULL);

	ata_device_detect(&ata_primary_master);
	ata_device_detect(&ata_primary_slave);
	ata_device_detect(&ata_secondary_master);
	ata_device_detect(&ata_secondary_slave);

	return 0;
}

static int ata_finalize(void) {

	return 0;
}

struct Module metadata = {
	.name = "ata",
	.init = ata_initialize,
	.fini = ata_finalize,
};

