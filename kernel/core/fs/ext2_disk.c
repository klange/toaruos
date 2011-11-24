#include <system.h>
#include <ext2.h>
#include <fs.h>

#define EXT2_DEBUG_BLOCK_DESCRIPTORS 1

#define BLOCKSIZE    1024
#define SECTORSIZE   512
#define CACHEENTRIES 512
#define DISK_PORT 0x1F0

typedef struct {
	uint32_t block_no;
	uint32_t last_use;
	uint8_t  block[BLOCKSIZE];
} ext2_disk_cache_entry_t;

ext2_disk_cache_entry_t *ext2_disk_cache   = NULL;
ext2_superblock_t * ext2_disk_superblock   = NULL;
ext2_bgdescriptor_t * ext2_disk_root_block = NULL;
fs_node_t * ext2_root_fsnode               = NULL;

uint32_t ext2_disk_node_from_file(ext2_inodetable_t * inode, ext2_dir_t * direntry, fs_node_t * fnode);

uint32_t ext2_disk_inodes_per_group = 0;
uint32_t ext2_disk_bg_descriptors = 0;

#define BGDS ext2_disk_bg_descriptors
#define SB ext2_disk_superblock
#define BGD ext2_disk_root_block
#define RN ext2_root_fsnode
#define DC ext2_disk_cache

#define BLOCKBIT(n) (bg_buffer[(n / 8)] & (1 << ((n % 8))))

static uint32_t btos(uint32_t block) {
	return block * (BLOCKSIZE / SECTORSIZE); 
}

void ext2_disk_read_block(uint32_t block_no, uint8_t * buf) {
	if (!block_no) return;
	int oldest = -1;
	uint32_t oldest_age = UINT32_MAX;
	for (uint32_t i = 0; i < CACHEENTRIES; ++i) {
		if (DC[i].block_no == block_no) {
			DC[i].last_use = now();
			memcpy(buf, &DC[i].block, BLOCKSIZE);
			return;
		}
		if (DC[i].last_use < oldest_age) {
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}
	ide_read_sector(DISK_PORT, 0, btos(block_no) + 0, (uint8_t *)((uint32_t)&(DC[oldest].block) + 0));
	ide_read_sector(DISK_PORT, 0, btos(block_no) + 1, (uint8_t *)((uint32_t)&(DC[oldest].block) + SECTORSIZE));
	memcpy(buf, &DC[oldest].block, BLOCKSIZE);
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = now();
}

void ext2_disk_write_block(uint32_t block_no, uint8_t * buf) {
	if (!block_no) return;
	ide_write_sector(DISK_PORT, 0, btos(block_no) + 0, (uint8_t *)((uint32_t)buf + 0));
	ide_write_sector(DISK_PORT, 0, btos(block_no) + 0, (uint8_t *)((uint32_t)buf + SECTORSIZE));
	int oldest = -1;
	uint32_t oldest_age = UINT32_MAX;
	for (uint32_t i = 0; i < CACHEENTRIES; ++i) {
		if (DC[i].block_no == block_no) {
			DC[i].last_use = now();
			memcpy(&DC[i].block, buf, BLOCKSIZE);
			return;
		}
		if (DC[i].last_use < oldest_age) {
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}
	memcpy(&DC[oldest].block, buf, BLOCKSIZE);
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = now();
}

uint32_t ext2_disk_inode_block(ext2_inodetable_t * inode, uint32_t block, uint8_t * buf) {
	if (block < 12) {
		ext2_disk_read_block(inode->block[block], buf);
		return inode->block[block];
	} else if (block < 12 + (BLOCKSIZE << SB->log_block_size) / sizeof(uint32_t)) {
		uint8_t * tmp = malloc(BLOCKSIZE);
		ext2_disk_read_block(inode->block[12], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[block - 12];
		free(tmp);
		ext2_disk_read_block(nblock, buf);
		return nblock;
	} else if (block < 12 + 256 + 256 * 256) {
		uint32_t a = block - 12;
		uint32_t b = a - 256;
		uint32_t c = b / 256;
		uint32_t d = b - c * 256;
		uint8_t * tmp = malloc(BLOCKSIZE);
		ext2_disk_read_block(inode->block[13], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[c];
		ext2_disk_read_block(nblock, tmp);
		nblock = ((uint32_t *)tmp)[d];
		free(tmp);
		ext2_disk_read_block(nblock, buf);
		return nblock;
	}
	HALT_AND_CATCH_FIRE("Attempted to read a file block that was too high :(", NULL);
	return 0;
}

ext2_dir_t * ext2_disk_direntry(ext2_inodetable_t * inode, uint32_t index) {
	uint8_t * block = malloc(BLOCKSIZE);
	ext2_disk_inode_block(inode,0,block);
	uint32_t dir_offset;
	dir_offset = 0;
	uint32_t dir_index;
	dir_index = 0;
	while (dir_offset < inode->size) {
		ext2_dir_t * d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		if (dir_index == index) {
			ext2_dir_t * out = malloc(d_ent->rec_len);
			memcpy(out, d_ent, d_ent->rec_len);
			free(block);
			return out;
		}
		dir_offset += d_ent->rec_len;
		dir_index++;
		/* XXX: if dir_offest > BLOCKSIZE, next block!!! */
	}
	free(block);
	return NULL;
}

ext2_inodetable_t * ext2_disk_inode(uint32_t inode) {
	uint32_t group = inode / ext2_disk_inodes_per_group;
	if (group > BGDS) { return NULL; }
	uint32_t inode_table_block = BGD[group].inode_table;
	inode -= group * ext2_disk_inodes_per_group;
	uint32_t block_offset      = ((inode - 1) * SB->inode_size) / BLOCKSIZE;
	uint32_t offset_in_block   = (inode - 1) - block_offset * (BLOCKSIZE / SB->inode_size);
	uint8_t  * buf             = malloc(BLOCKSIZE);
	ext2_inodetable_t * inodet = malloc(sizeof(ext2_inodetable_t));
	ext2_disk_read_block(inode_table_block + block_offset, buf);
	ext2_inodetable_t * inodes = (ext2_inodetable_t *)buf;
	memcpy(inodet, &inodes[offset_in_block], sizeof(ext2_inodetable_t));

	free(buf);
	return inodet;
}

uint32_t read_ext2_disk (
		fs_node_t *node,
		uint32_t offset,
		uint32_t size,
		uint8_t *buffer
		) {
	ext2_inodetable_t * inode = ext2_disk_inode(node->inode);
	uint32_t end;
	if (offset + size > inode->size) {
		end = inode->size;
	} else {
		end = offset + size;
	}
	uint32_t start_block = offset / BLOCKSIZE;
	uint32_t end_block   = end / BLOCKSIZE;
	uint32_t end_size    = end % BLOCKSIZE;
	uint32_t size_to_read = end - offset;
	if (end_size == 0) { end_block--; }
	if (start_block == end_block) {
		void * buf = malloc(BLOCKSIZE);
		ext2_disk_inode_block(inode, start_block, buf);
		memcpy(buffer, (uint8_t *)(((uint32_t)buf) + offset % BLOCKSIZE), size_to_read);
		free(buf);
		return size_to_read;
	} else {
		uint32_t block_offset = start_block;
		uint32_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; ++block_offset) {
			if (block_offset == start_block) {
				void * buf = malloc(BLOCKSIZE);
				ext2_disk_inode_block(inode, block_offset, buf);
				memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), (BLOCKSIZE - (offset % BLOCKSIZE)));
				free(buf);
			} else {
				void * buf = malloc(BLOCKSIZE);
				ext2_disk_inode_block(inode, block_offset, buf);
				memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, BLOCKSIZE);
				free(buf);
			}
			blocks_read++;
		}
		void * buf = malloc(BLOCKSIZE);
		ext2_disk_inode_block(inode, end_block, buf);
		memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, end_size);
		free(buf);
	}
	free(inode);
	return size_to_read;
}

void
open_ext2_disk (
		fs_node_t *node,
		uint8_t read,
		uint8_t write
		) {
	// woosh
}

struct dirent *
readdir_ext2_disk (
		fs_node_t *node,
		uint32_t index
		) {
	ext2_inodetable_t * inode = ext2_disk_inode(node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	ext2_dir_t * direntry = ext2_disk_direntry(inode, index);
	if (!direntry) {
		return NULL;
	}
	struct dirent * dirent = malloc(sizeof(struct dirent));
	memcpy(&dirent->name, &direntry->name, direntry->name_len);
	dirent->name[direntry->name_len] = '\0';
	dirent->ino = direntry->inode;
	free(direntry);
	free(inode);
	return dirent;
}

fs_node_t *
finddir_ext2_disk (
		fs_node_t *node,
		char *name
		) {
	/*
	 * Find the actual inode in the ramdisk image for the requested file
	 */
	ext2_inodetable_t * inode = ext2_disk_inode(node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	void * block = malloc(BLOCKSIZE);
	ext2_dir_t * direntry = NULL;
	ext2_disk_inode_block(inode, 0, block);
	uint32_t dir_offset;
	dir_offset = 0;
	/*
	 * Look through the requested entries until we find what we're looking for
	 */
	while (dir_offset < inode->size) {
		ext2_dir_t * d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		if (strlen(name) != d_ent->name_len) {
			dir_offset += d_ent->rec_len;
			continue;
		}
		char * dname = malloc(sizeof(char) * (d_ent->name_len + 1));
		memcpy(dname, &d_ent->name, d_ent->name_len);
		dname[d_ent->name_len] = '\0';
		if (!strcmp(dname, name)) {
			free(dname);
			direntry = malloc(d_ent->rec_len);
			memcpy(direntry, d_ent, d_ent->rec_len);
			break;
		}
		free(dname);
		dir_offset += d_ent->rec_len;
	}
	free(inode);
	free(block);
	if (!direntry) {
		/*
		 * We could not find the requested entry in this directory.
		 */
		return NULL;
	}
	fs_node_t * outnode = malloc(sizeof(fs_node_t));
	inode = ext2_disk_inode(direntry->inode);
	ext2_disk_node_from_file(inode, direntry, outnode);
	free(inode);
	return outnode;
}


uint32_t ext2_disk_node_from_file(ext2_inodetable_t * inode, ext2_dir_t * direntry, fs_node_t * fnode) {
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
	if ((inode->mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
		fnode->flags |= FS_FILE;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
	}
	if ((inode->mode & EXT2_S_IFBLK) == EXT2_S_IFBLK) {
		fnode->flags |= FS_BLOCKDEVICE;
	}
	if ((inode->mode & EXT2_S_IFCHR) == EXT2_S_IFCHR) {
		fnode->flags |= FS_CHARDEVICE;
	}
	if ((inode->mode & EXT2_S_IFIFO) == EXT2_S_IFIFO) {
		fnode->flags |= FS_PIPE;
	}
	if ((inode->mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
		fnode->flags |= FS_SYMLINK;
	}
	fnode->read    = read_ext2_disk;
	fnode->write   = NULL; //write_ext2_disk;
	fnode->open    = open_ext2_disk;
	fnode->close   = NULL; //close_ext2_disk;
	fnode->readdir = readdir_ext2_disk;
	fnode->finddir = finddir_ext2_disk;
	return 1;
}


uint32_t ext2_disk_node_root(ext2_inodetable_t * inode, fs_node_t * fnode) {
	if (!fnode) {
		return 0;
	}
	/* Information from the direntry */
	fnode->inode = 2;
	fnode->name[0] = '/';
	fnode->name[1] = '\0';
	/* Information from the inode */
	fnode->uid = inode->uid;
	fnode->gid = inode->gid;
	fnode->length = inode->size;
	fnode->mask = inode->mode & 0xFFF;
	/* File Flags */
	fnode->flags = 0;
	if ((inode->mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
		fnode->flags |= FS_FILE;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
	}
	if ((inode->mode & EXT2_S_IFBLK) == EXT2_S_IFBLK) {
		fnode->flags |= FS_BLOCKDEVICE;
	}
	if ((inode->mode & EXT2_S_IFCHR) == EXT2_S_IFCHR) {
		fnode->flags |= FS_CHARDEVICE;
	}
	if ((inode->mode & EXT2_S_IFIFO) == EXT2_S_IFIFO) {
		fnode->flags |= FS_PIPE;
	}
	if ((inode->mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
		fnode->flags |= FS_SYMLINK;
	}
	fnode->read    = read_ext2_disk;
	fnode->write   = NULL; //write_ext2_disk;
	fnode->open    = open_ext2_disk;
	fnode->close   = NULL; //close_ext2_disk;
	fnode->readdir = readdir_ext2_disk;
	fnode->finddir = finddir_ext2_disk;
	return 1;
}

void ext2_disk_read_superblock() {
	kprintf("Volume '%s'\n", SB->volume_name);
	kprintf("%d inodes\n", SB->inodes_count);
	kprintf("%d blocks\n", SB->blocks_count);
	kprintf("%d free blocks\n", SB->free_blocks_count);
	kprintf("0x%x last mount time\n", SB->mtime);
	kprintf("0x%x last write time\n", SB->wtime);
	kprintf("Mounted %d times.\n", SB->mnt_count);
	kprintf("0x%x\n", SB->magic);
}

void ext2_disk_mount() {
	DC = malloc(sizeof(ext2_disk_cache_entry_t) * CACHEENTRIES);
	SB = malloc(BLOCKSIZE);
	ext2_disk_read_block(1, (uint8_t *)SB);
	assert(SB->magic == EXT2_SUPER_MAGIC);
	if (SB->inode_size == 0) {
		SB->inode_size = 128;
	}
	BGDS = SB->blocks_count / SB->blocks_per_group;
	ext2_disk_inodes_per_group = SB->inodes_count / BGDS;

	ext2_disk_root_block = malloc(BGDS * sizeof(ext2_bgdescriptor_t *));
	ext2_disk_read_block(2, (uint8_t *)BGD);

#if EXT2_DEBUG_BLOCK_DESCRIPTORS
	char bg_buffer[BLOCKSIZE];
	for (uint32_t i = 0; i < BGDS; ++i) {
		kprintf("Block Group Descriptor #%d @ %d\n", i, 2 + i * SB->blocks_per_group);
		kprintf("\tBlock Bitmap @ %d\n", BGD[i].block_bitmap); { 
			kprintf("\t\tExamining block bitmap at %d\n", BGD[i].block_bitmap);
			ext2_disk_read_block(BGD[i].block_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			kprintf("\t\tFirst free block in group is %d\n", j + BGD[i].block_bitmap - 2);
		}
		kprintf("\tInode Bitmap @ %d\n", BGD[i].inode_bitmap); {
			kprintf("\t\tExamining inode bitmap at %d\n", BGD[i].inode_bitmap);
			ext2_disk_read_block(BGD[i].inode_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			kprintf("\t\tFirst free inode in group is %d\n", j + ext2_disk_inodes_per_group * i + 1);
		}
		kprintf("\tInode Table  @ %d\n", BGD[i].inode_table);
		kprintf("\tFree Blocks =  %d\n", BGD[i].free_blocks_count);
		kprintf("\tFree Inodes =  %d\n", BGD[i].free_inodes_count);
	}
#endif

	ext2_inodetable_t * root_inode = ext2_disk_inode(2);

	RN = (fs_node_t *)malloc(sizeof(fs_node_t));
	assert(ext2_disk_node_root(root_inode, RN));
	fs_root = RN;
}

void ext2_disk_forget_superblock() {
	free(SB);
}
