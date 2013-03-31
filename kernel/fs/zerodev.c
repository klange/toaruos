/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * zero Device
 */

#include <system.h>
#include <fs.h>

uint32_t read_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_zero(fs_node_t *node, uint8_t read, uint8_t write);
void close_zero(fs_node_t *node);

uint32_t read_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	memset(buffer, 0x00, size);
	return 1;
}

uint32_t write_zero(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

void open_zero(fs_node_t * node, uint8_t read, uint8_t write) {
	return;
}

void close_zero(fs_node_t * node) {
	return;
}

fs_node_t * zero_device_create() {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "zero");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_zero;
	fnode->write   = write_zero;
	fnode->open    = open_zero;
	fnode->close   = close_zero;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

