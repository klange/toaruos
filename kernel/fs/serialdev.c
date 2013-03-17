/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Serial communication device
 *
 */

#include <system.h>
#include <fs.h>
#include <logging.h>

uint32_t read_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_serial(fs_node_t *node, uint8_t read, uint8_t write);
void close_serial(fs_node_t *node);

uint32_t read_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (size < 1) {
		return 0;
	}
	memset(buffer, 0x00, 1);
	uint32_t collected = 0;
	while (collected < size) {
		while (!serial_rcvd(node->inode)) {
			switch_task(1);
		}
		debug_print(NOTICE, "Data received from TTY");
		buffer[collected] = serial_recv(node->inode);
		collected++;
	}
	return collected;
}

uint32_t write_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	uint32_t sent = 0;
	while (sent < size) {
		serial_send(node->inode, buffer[sent]);
		sent++;
	}
	return size;
}

void open_serial(fs_node_t * node, uint8_t read, uint8_t write) {
	return;
}

void close_serial(fs_node_t * node) {
	return;
}

fs_node_t * serial_device_create(int device) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = device;
	strcpy(fnode->name, "serial");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = read_serial;
	fnode->write   = write_serial;
	fnode->open    = open_serial;
	fnode->close   = close_serial;
	fnode->readdir = NULL;
	fnode->finddir = NULL;

	fnode->atime = now();
	fnode->mtime = fnode->atime;
	fnode->ctime = fnode->atime;

	return fnode;
}

void serial_mount_devices() {

	fs_node_t * ttyS0 = serial_device_create(SERIAL_PORT_A);
	vfs_mount("/dev/ttyS0", ttyS0);

	fs_node_t * ttyS1 = serial_device_create(SERIAL_PORT_B);
	vfs_mount("/dev/ttyS1", ttyS1);

	fs_node_t * ttyS2 = serial_device_create(SERIAL_PORT_C);
	vfs_mount("/dev/ttyS2", ttyS2);

	fs_node_t * ttyS3 = serial_device_create(SERIAL_PORT_D);
	vfs_mount("/dev/ttyS3", ttyS3);

}
