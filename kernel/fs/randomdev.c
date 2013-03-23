/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * A device that returns "hello world" when read from.
 */

#include <system.h>
#include <logging.h>
#include <fs.h>

uint32_t read_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_random(fs_node_t *node, uint8_t read, uint8_t write);
void close_random(fs_node_t *node);

uint32_t read_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	uint32_t s = 0;
	while (s < size) {
		buffer[s] = krand() % 0xFF;
		offset++;
		s++;
	}
	return size;
}

uint32_t write_random(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return size;
}

void open_random(fs_node_t * node, uint8_t read, uint8_t write) {
	return;
}

void close_random(fs_node_t * node) {
	return;
}

fs_node_t * random_device_create() {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "random");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->length  = 1024;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_random;
	fnode->write   = write_random;
	fnode->open    = open_random;
	fnode->close   = close_random;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}
