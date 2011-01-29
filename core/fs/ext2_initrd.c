#include <system.h>
#include <ext2.h>
#include <fs.h>

ext2_superblock_t * initrd_superblock;
ext2_inodetable_t * initrd_root_node;
fs_node_t *         initrd_root;
fs_node_t *         initrd_dev;

uint32_t initrd_node_from_file(ext2_inodetable_t *inode, ext2_dir_t *direntry, fs_node_t *fnode);
uint32_t read_initrd(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_initrd(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_initrd(fs_node_t *node, uint8_t read, uint8_t write);
void close_initrd(fs_node_t *node);
struct dirent *readdir_initrd(fs_node_t *node, uint32_t index);
fs_node_t *finddir_initrd(fs_node_t *node, char *name);

uint32_t
read_initrd(
		fs_node_t *node,
		uint32_t offset,
		uint32_t size,
		uint8_t *buffer
		) {
	return 0;
}

uint32_t
write_initrd(
		fs_node_t *node,
		uint32_t offset,
		uint32_t size,
		uint8_t *buffer
		) {
	/*
	 * Not implemented
	 */
	return 0;
}

void
open_initrd(
		fs_node_t *node,
		uint8_t read,
		uint8_t write
		) {
	// woosh
}

void
close_initrd(
		fs_node_t *node
		) {
	/*
	 * Nothing to do here
	 */
}

struct dirent *
readdir_initrd(
		fs_node_t *node,
		uint32_t index
		) {
	return NULL;
}

fs_node_t *
finddir_initrd(
		fs_node_t *node,
		char *name
		) {
	return NULL;
}

uint32_t
initrd_node_from_file(
		ext2_inodetable_t * inode,
		ext2_dir_t * direntry,
		fs_node_t * fnode
		) {
	if (!fnode) {
		/* You didn't give me a node to write into, go *** yourself */
		return 0;
	}
	/* Information from the direntry */
	fnode->inode = direntry->inode;
	memcpy(&fnode->name, &direntry->name, direntry->name_len);
	fnode->name[direntry->name_len] = '\0';
	/* Information from the inode */
	fnode->uid = inode->uid;
	fnode->gid = inode->gid;
	fnode->length = inode->size;
	fnode->mask = inode->mode & 0xFFF;
	/* File Flags */
	fnode->flags = 0;
	if (inode->mode & EXT2_S_IFREG) {
		fnode->flags &= FS_FILE;
	}
	if (inode->mode & EXT2_S_IFDIR) {
		fnode->flags &= FS_DIRECTORY;
	}
	if (inode->mode & EXT2_S_IFBLK) {
		fnode->flags &= FS_BLOCKDEVICE;
	}
	if (inode->mode & EXT2_S_IFCHR) {
		fnode->flags &= FS_CHARDEVICE;
	}
	if (inode->mode & EXT2_S_IFIFO) {
		fnode->flags &= FS_PIPE;
	}
	if (inode->mode & EXT2_S_IFLNK) {
		fnode->flags &= FS_SYMLINK;
	}
	fnode->read    = read_initrd;
	fnode->write   = write_initrd;
	fnode->open    = open_initrd;
	fnode->close   = close_initrd;
	fnode->readdir = readdir_initrd;
	fnode->finddir = finddir_initrd;
}
