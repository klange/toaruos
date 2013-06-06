/*
 * ToAruOS DevFS
 *
 */
#include <system.h>
#include <fs.h>

uint32_t read_devfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_devfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_devfs(fs_node_t *node, uint8_t read, uint8_t write);
void close_devfs(fs_node_t *node);
struct dirent *readdir_devfs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_devfs(fs_node_t *node, char *name);

fs_node_t * devfs_root;

/*
 * Install the DevFS to the given path.
 * Path should be `/dev`
 */
void
devfs_install(char * path) {
	fs_node_t * dev_node = kopen(path,0);
	kprintf("Installing devfs... %s\n", dev_node->name);
}

/*
 * These functions require that the requested node have a valid handler of their own
 * and are not part of the devfs natively
 */
uint32_t read_devfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return -1;
}
uint32_t write_devfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return -1;
}


fs_node_t *
devfs_create_keyboard(void) {
	return NULL;
}

/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
