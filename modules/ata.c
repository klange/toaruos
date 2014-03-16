/*
 * ATA Disk Driver
 *
 * Provides raw block access to an (Parallel) ATA drive.
 */

#include <system.h>
#include <logging.h>
#include <module.h>
#include <fs.h>

/* TODO: Move this to mod/ata.h */
#include <ata.h>
#include <fs.h>

static char ata_drive_char = 'a';

struct ata_device {
	int io_base;
	int control;
	int slave;
};

static uint32_t read_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t write_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void     open_ata(fs_node_t *node, unsigned int flags);
static void     close_ata(fs_node_t *node);

static uint32_t read_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	/* Do nothing */
	return size;
}

static uint32_t write_ata(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return size;
}

static void open_ata(fs_node_t * node, unsigned int flags) {
	return;
}

static void close_ata(fs_node_t * node) {
	return;
}

fs_node_t * ata_device_create(struct ata_device * device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	sprintf(fnode->name, "atadev%d", ata_drive_char - 'a');
	fnode->device  = device;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->length  = 0; /* TODO */
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

void ata_io_wait(struct ata_device * dev) {
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
	inportb(dev->io_base + ATA_REG_ALTSTATUS);
}


static int ata_wait(struct ata_device * dev, int advanced) {
	uint8_t status = 0;

	ata_io_wait(dev);

	while ((status = inportb(dev->io_base + ATA_REG_STATUS)) & ATA_SR_BSY);

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
	outportb(dev->control, 0x00);
}

static int ata_device_detect(struct ata_device * dev) {
	ata_soft_reset(dev);
	outportb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | dev->slave << 4);
	ata_io_wait(dev);

	unsigned char cl = inportb(dev->io_base + ATA_REG_LBA1); /* CYL_LO */
	unsigned char ch = inportb(dev->io_base + ATA_REG_LBA2); /* CYL_HI */

	debug_print(NOTICE, "0x%x 0x%x", cl, ch);
	if (cl == 0xFF && ch == 0xFF) {
		/* Nothing here */
		return 0;
	}
	if (cl == 0x00 && ch == 0x00) {
		/* Parallel ATA device */

		char devname[64];
		sprintf((char *)&devname, "/dev/hd%c", ata_drive_char);
		fs_node_t * node = ata_device_create(dev);
		vfs_mount(devname, node);
		ata_drive_char++;

		return 1;
	}

	/* TODO: ATAPI, SATA, SATAPI */
	return 0;
}

static struct ata_device ata_primary_master   = {0x1F0, 0x3F6, 0};
static struct ata_device ata_primary_slave    = {0x1F0, 0x3F6, 1};
static struct ata_device ata_secondary_master = {0x170, 0x376, 0};
static struct ata_device ata_secondary_slave  = {0x170, 0x376, 1};


static int ata_initialize(void) {
	/* Detect drives and mount them */

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
