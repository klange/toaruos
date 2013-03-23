/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * A device that returns "hello world" when read from.
 */

#include <system.h>
#include <fs.h>

uint32_t read_hello(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_hello(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_hello(fs_node_t *node, uint8_t read, uint8_t write);
void close_hello(fs_node_t *node);

char hello[] = "hello world";

uint32_t read_hello(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	uint32_t s = 0;
	int d = strlen(hello);
	while (s < size) {
		buffer[s] = hello[offset % d];
		offset++;
		s++;
	}
	return size;
}

uint32_t write_hello(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return size;
}

void open_hello(fs_node_t * node, uint8_t read, uint8_t write) {
	return;
}

void close_hello(fs_node_t * node) {
	return;
}

fs_node_t * hello_device_create() {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "hello");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->length  = strlen(hello);
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_hello;
	fnode->write   = write_hello;
	fnode->open    = open_hello;
	fnode->close   = close_hello;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}
