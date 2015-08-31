/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
  *
 * ATA Disk Driver
 *
 * Provides raw block access to an (Parallel) ATA drive.
 */

#include <system.h>
#include <logging.h>
#include <module.h>
#include <fs.h>
#include <printf.h>

#include <pci.h>

/* TODO: Move this to mod/ata.h */
#include <ata.h>

static char ata_drive_char = 'a';
static uint32_t ata_pci = 0x00000000;

/* 8086:7010 */
static void find_ata_pci(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if ((vendorid == 0x8086) && (deviceid == 0x7010)) {
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
	ata_identify_t identity;
	prdt_t * dma_prdt;
	uintptr_t dma_prdt_phys;
	uint8_t * dma_start;
	uintptr_t dma_start_phys;
	uint32_t bar4;
};

static struct ata_device ata_primary_master   = {.io_base = 0x1F0, .control = 0x3F6, .slave = 0};
static struct ata_device ata_primary_slave    = {.io_base = 0x1F0, .control = 0x3F6, .slave = 1};
static struct ata_device ata_secondary_master = {.io_base = 0x170, .control = 0x376, .slave = 0};
static struct ata_device ata_secondary_slave  = {.io_base = 0x170, .control = 0x376, .slave = 1};

//static volatile uint8_t ata_lock = 0;
static spin_lock_t ata_lock = { 0 };

/* TODO support other sector sizes */
#define ATA_SECTOR_SIZE 512

static void ata_device_read_sector(struct ata_device * dev, uint32_t lba, uint8_t * buf);
static void ata_device_write_sector_retry(struct ata_device * dev, uint32_t lba, uint8_t * buf);
static uint32_t read_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void     open_ata(fs_node_t *node, unsigned int flags);
static void     close_ata(fs_node_t *node);

static uint64_t ata_max_offset(struct ata_device * dev) {
	uint64_t sectors = dev->identity.sectors_48;
	if (!sectors) {
		/* Fall back to sectors_28 */
		sectors = dev->identity.sectors_28;
	}

	return sectors * ATA_SECTOR_SIZE;
}

static uint32_t read_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {

	struct ata_device * dev = (struct ata_device *)node->device;

	unsigned int start_block = offset / ATA_SECTOR_SIZE;
	unsigned int end_block = (offset + size - 1) / ATA_SECTOR_SIZE;

	unsigned int x_offset = 0;

	if (offset > ata_max_offset(dev)) {
		return 0;
	}

	if (offset + size > ata_max_offset(dev)) {
		unsigned int i = ata_max_offset(dev) - offset;
		size = i;
	}

	if (offset % ATA_SECTOR_SIZE) {
		unsigned int prefix_size = (ATA_SECTOR_SIZE - (offset % ATA_SECTOR_SIZE));
		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, start_block, (uint8_t *)tmp);

		memcpy(buffer, (void *)((uintptr_t)tmp + (offset % ATA_SECTOR_SIZE)), prefix_size);

		free(tmp);

		x_offset += prefix_size;
		start_block++;
	}

	if ((offset + size)  % ATA_SECTOR_SIZE && start_block < end_block) {
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

static uint32_t write_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct ata_device * dev = (struct ata_device *)node->device;

	unsigned int start_block = offset / ATA_SECTOR_SIZE;
	unsigned int end_block = (offset + size - 1) / ATA_SECTOR_SIZE;

	unsigned int x_offset = 0;

	if (offset > ata_max_offset(dev)) {
		return 0;
	}

	if (offset + size > ata_max_offset(dev)) {
		unsigned int i = ata_max_offset(dev) - offset;
		size = i;
	}

	if (offset % ATA_SECTOR_SIZE) {
		unsigned int prefix_size = (ATA_SECTOR_SIZE - (offset % ATA_SECTOR_SIZE));

		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, start_block, (uint8_t *)tmp);

		debug_print(NOTICE, "Writing first block");

		memcpy((void *)((uintptr_t)tmp + (offset % ATA_SECTOR_SIZE)), buffer, prefix_size);
		ata_device_write_sector_retry(dev, start_block, (uint8_t *)tmp);

		free(tmp);
		x_offset += prefix_size;
		start_block++;
	}

	if ((offset + size)  % ATA_SECTOR_SIZE && start_block < end_block) {
		unsigned int postfix_size = (offset + size) % ATA_SECTOR_SIZE;

		char * tmp = malloc(ATA_SECTOR_SIZE);
		ata_device_read_sector(dev, end_block, (uint8_t *)tmp);

		debug_print(NOTICE, "Writing last block");

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

static fs_node_t * ata_device_create(struct ata_device * device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	sprintf(fnode->name, "atadev%d", ata_drive_char - 'a');
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
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
	irq_ack(14);
	return 1;
}

static int ata_irq_handler_s(struct regs *r) {
	inportb(ata_secondary_master.io_base + ATA_REG_STATUS);
	return 1;
}

static void ata_device_init(struct ata_device * dev) {

	debug_print(NOTICE, "Initializing IDE device on bus %d", dev->io_base);

	outportb(dev->io_base + 1, 1);
	outportb(dev->control, 0);

	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	outportb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	ata_io_wait(dev);

	int status = inportb(dev->io_base + ATA_REG_COMMAND);
	debug_print(INFO, "Device status: %d", status);

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

	debug_print(NOTICE, "Device Name:  %s", dev->identity.model);
	debug_print(NOTICE, "Sectors (48): %d", (uint32_t)dev->identity.sectors_48);
	debug_print(NOTICE, "Sectors (24): %d", dev->identity.sectors_28);

	debug_print(NOTICE, "Setting up DMA...");
	dev->dma_prdt  = (void *)kvmalloc_p(sizeof(prdt_t) * 1, &dev->dma_prdt_phys);
	dev->dma_start = (void *)kvmalloc_p(4096, &dev->dma_start_phys);

	debug_print(NOTICE, "Putting prdt    at 0x%x (0x%x phys)", dev->dma_prdt, dev->dma_prdt_phys);
	debug_print(NOTICE, "Putting prdt[0] at 0x%x (0x%x phys)", dev->dma_start, dev->dma_start_phys);

	dev->dma_prdt[0].offset = dev->dma_start_phys;
	dev->dma_prdt[0].bytes = 512;
	dev->dma_prdt[0].last = 0x8000;

	debug_print(NOTICE, "ATA PCI device ID: 0x%x", ata_pci);

	uint16_t command_reg = pci_read_field(ata_pci, PCI_COMMAND, 4);
	debug_print(NOTICE, "COMMAND register before: 0x%4x", command_reg);
	if (command_reg & (1 << 2)) {
		debug_print(NOTICE, "Bus mastering already enabled.");
	} else {
		command_reg |= (1 << 2); /* bit 2 */
		debug_print(NOTICE, "Enabling bus mastering...");
		pci_write_field(ata_pci, PCI_COMMAND, 4, command_reg);
		command_reg = pci_read_field(ata_pci, PCI_COMMAND, 4);
		debug_print(NOTICE, "COMMAND register after: 0x%4x", command_reg);
	}

	dev->bar4 = pci_read_field(ata_pci, PCI_BAR4, 4);
	debug_print(NOTICE, "BAR4: 0x%x", dev->bar4);

	if (dev->bar4 & 0x00000001) {
		dev->bar4 = dev->bar4 & 0xFFFFFFFC;
	} else {
		debug_print(WARNING, "? ATA bus master registers are /usually/ I/O ports.\n");
		return; /* No DMA because we're not sure what to do here */
	}

#if 0
	pci_write_field(ata_pci, PCI_INTERRUPT_LINE, 1, 0xFE);
	if (pci_read_field(ata_pci, PCI_INTERRUPT_LINE, 1) == 0xFE) {
		/* needs assignment */
		pci_write_field(ata_pci, PCI_INTERRUPT_LINE, 1, 14);
	}
#endif

}

static int ata_device_detect(struct ata_device * dev) {
	ata_soft_reset(dev);
	ata_io_wait(dev);
	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);
	ata_status_wait(dev, 10000);

	unsigned char cl = inportb(dev->io_base + ATA_REG_LBA1); /* CYL_LO */
	unsigned char ch = inportb(dev->io_base + ATA_REG_LBA2); /* CYL_HI */

	debug_print(NOTICE, "Device detected: 0x%2x 0x%2x", cl, ch);
	if (cl == 0xFF && ch == 0xFF) {
		/* Nothing here */
		return 0;
	}
	if ((cl == 0x00 && ch == 0x00) ||
	    (cl == 0x3C && ch == 0xC3)) {
		/* Parallel ATA device, or emulated SATA */

		char devname[64];
		sprintf((char *)&devname, "/dev/hd%c", ata_drive_char);
		fs_node_t * node = ata_device_create(dev);
		vfs_mount(devname, node);
		ata_drive_char++;

		ata_device_init(dev);

		return 1;
	}

	/* TODO: ATAPI, SATA, SATAPI */
	return 0;
}

static void ata_device_read_sector(struct ata_device * dev, uint32_t lba, uint8_t * buf) {
	uint16_t bus = dev->io_base;
	uint8_t slave = dev->slave;

	spin_lock(ata_lock);

#if 0
	int errors = 0;
try_again:
#endif

	ata_wait(dev, 0);

	/* Stop */
	outportb(dev->bar4, 0x00);

	/* Set the PRDT */
	outportl(dev->bar4 + 0x04, dev->dma_prdt_phys);

	/* Enable error, irq status */
	outportb(dev->bar4 + 0x2, inportb(dev->bar4 + 0x02) | 0x04 | 0x02);

	/* set read */
	outportb(dev->bar4, 0x08);

	IRQ_ON;
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if (!(status & ATA_SR_BSY)) break;
	}

	outportb(bus + ATA_REG_CONTROL, 0x00);
	outportb(bus + ATA_REG_HDDEVSEL, 0xe0 | slave << 4 | (lba & 0x0f000000) >> 24);
	ata_io_wait(dev);
	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_SECCOUNT0, 1);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	//outportb(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
#if 1
	while (1) {
		uint8_t status = inportb(dev->io_base + ATA_REG_STATUS);
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) break;
	}
#endif
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_READ_DMA);

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
	IRQ_OFF;

#if 0
	if (ata_wait(dev, 1)) {
		debug_print(WARNING, "Error during ATA read of lba block %d", lba);
		errors++;
		if (errors > 4) {
			debug_print(WARNING, "-- Too many errors trying to read this block. Bailing.");
			spin_unlock(ata_lock);
			return;
		}
		goto try_again;
	}
#endif

	/* Copy from DMA buffer to output buffer. */
	memcpy(buf, dev->dma_start, 512);

	/* Inform device we are done. */
	outportb(dev->bar4 + 0x2, inportb(dev->bar4 + 0x02) | 0x04 | 0x02);

#if 0
	int size = 256;
	inportsm(bus,buf,size);
	ata_wait(dev, 0);
	outportb(bus + ATA_REG_CONTROL, 0x02);
#endif
	spin_unlock(ata_lock);
}

static void ata_device_write_sector(struct ata_device * dev, uint32_t lba, uint8_t * buf) {
	uint16_t bus = dev->io_base;
	uint8_t slave = dev->slave;

	spin_lock(ata_lock);

	outportb(bus + ATA_REG_CONTROL, 0x02);

	ata_wait(dev, 0);
	outportb(bus + ATA_REG_HDDEVSEL, 0xe0 | slave << 4 | (lba & 0x0f000000) >> 24);
	ata_wait(dev, 0);

	outportb(bus + ATA_REG_FEATURES, 0x00);
	outportb(bus + ATA_REG_SECCOUNT0, 0x01);
	outportb(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	outportb(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	outportb(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	outportb(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
	ata_wait(dev, 0);
	int size = ATA_SECTOR_SIZE / 2;
	outportsm(bus,buf,size);
	outportb(bus + 0x07, ATA_CMD_CACHE_FLUSH);
	ata_wait(dev, 0);
	spin_unlock(ata_lock);
}

static int buffer_compare(uint32_t * ptr1, uint32_t * ptr2, size_t size) {
	assert(!(size % 4));
	size_t i = 0;
	while (i < size) {
		if (*ptr1 != *ptr2) return 1;
		ptr1++;
		ptr2++;
		i += sizeof(uint32_t);
	}
	return 0;
}

static void ata_device_write_sector_retry(struct ata_device * dev, uint32_t lba, uint8_t * buf) {
	uint8_t * read_buf = malloc(ATA_SECTOR_SIZE);
	do {
		ata_device_write_sector(dev, lba, buf);
		ata_device_read_sector(dev, lba, read_buf);
	} while (buffer_compare((uint32_t *)buf, (uint32_t *)read_buf, ATA_SECTOR_SIZE));
	free(read_buf);
}

static int ata_initialize(void) {
	/* Detect drives and mount them */

	/* Locate ATA device via PCI */
	pci_scan(&find_ata_pci, -1, &ata_pci);

	irq_install_handler(14, ata_irq_handler);
#if 0
	irq_install_handler(15, ata_irq_handler_s);
#endif

	ata_device_detect(&ata_primary_master);
	ata_device_detect(&ata_primary_slave);
	ata_device_detect(&ata_secondary_master);
	ata_device_detect(&ata_secondary_slave);

	return 0;
}

static int ata_finalize(void) {

	return 0;
}

MODULE_DEF(ata, ata_initialize, ata_finalize);
