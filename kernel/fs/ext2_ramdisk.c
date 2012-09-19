/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#include <system.h>
#include <ext2.h>
#include <fs.h>
#include <logging.h>

#define EXT2_DEBUG_BLOCK_DESCRIPTORS 0

#define ramdisk_PORT	0
#define BLOCKSIZE		1024
#define SECTORSIZE		512
#define CACHEENTRIES	10240

typedef struct {
	uint32_t block_no;
	uint32_t last_use;
	uint8_t  dirty;
	uint8_t  block[BLOCKSIZE];
} ext2_ramdisk_cache_entry_t;

uint32_t _ramdisk_offset = 0;

ext2_ramdisk_cache_entry_t *ext2_ramdisk_cache   = NULL;	// LSU block cache
ext2_superblock_t *ext2_ramdisk_superblock    = NULL;
ext2_bgdescriptor_t *ext2_ramdisk_root_block  = NULL;
static fs_node_t *ext2_root_fsnode                = NULL;

/** Prototypes */
uint32_t ext2_ramdisk_node_from_file(ext2_inodetable_t *inode, ext2_dir_t *direntry, fs_node_t *fnode);
ext2_inodetable_t *ext2_ramdisk_inode(uint32_t inode);
ext2_inodetable_t *ext2_ramdisk_alloc_inode(ext2_inodetable_t *parent, uint32_t no, char *name, uint16_t mode, uint32_t *inode_no);
fs_node_t *finddir_ext2_ramdisk(fs_node_t *node, char *name);
void insertdir_ext2_ramdisk(ext2_inodetable_t *p_node, uint32_t no, uint32_t inode, char *name, uint8_t type);
void ext2_ramdisk_write_inode(ext2_inodetable_t *inode, uint32_t index);
uint32_t write_ext2_ramdisk(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t read_ext2_ramdisk(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

uint32_t ext2_ramdisk_inodes_per_group = 0;
uint32_t ext2_ramdisk_bg_descriptors = 0;		// Total number of block groups

#define BGDS ext2_ramdisk_bg_descriptors
#define SB ext2_ramdisk_superblock
#define BGD ext2_ramdisk_root_block
#define RN ext2_root_fsnode
#define DC ext2_ramdisk_cache

#define BLOCKBIT(n)  (bg_buffer[((n) >> 3)] & (1 << (((n) % 8))))
#define BLOCKBYTE(n) (bg_buffer[((n) >> 3)])
#define SETBIT(n)    (1 << (((n) % 8)))

static uint32_t btos(uint32_t block) {
	return block * (BLOCKSIZE / SECTORSIZE); 
}

static uint8_t volatile lock;

static uint32_t _now = 1;
static uint32_t ext2_time() {
	return _now++;
}

void ramdisk_read_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	char * src = (char *)(lba * SECTORSIZE + _ramdisk_offset);
	memcpy(buf, src, SECTORSIZE);
}
void ramdisk_write_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	return;
}

static void ext2_flush_dirty(uint32_t ent_no) {
	// write out to the ramdisk
	for (uint32_t i = 0; i < BLOCKSIZE / SECTORSIZE; ++i) {
		ramdisk_write_sector(ramdisk_PORT, 0, btos(DC[ent_no].block_no) + i, (uint8_t *)((uint32_t)&DC[ent_no].block + SECTORSIZE * i));
	}
	DC[ent_no].dirty = 0;
}

void ext2_ramdisk_read_block(uint32_t block_no, uint8_t *buf) {
	if (!block_no) return;
	spin_lock(&lock);
	int oldest = -1;
	uint32_t oldest_age = UINT32_MAX;
	for (uint32_t i = 0; i < CACHEENTRIES; ++i) {
		if (DC[i].block_no == block_no) {
			DC[i].last_use = ext2_time();
			memcpy(buf, &DC[i].block, BLOCKSIZE);
			spin_unlock(&lock);
			return;
		}
		if (DC[i].last_use < oldest_age) {
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}

	for (uint32_t i = 0; i < BLOCKSIZE / SECTORSIZE; ++i) {
		ramdisk_read_sector(ramdisk_PORT, 0, btos(block_no) + i, (uint8_t *)((uint32_t)&(DC[oldest].block) + SECTORSIZE * i));
	}

	if (DC[oldest].dirty) {
		ext2_flush_dirty(oldest);
	}
	memcpy(buf, &DC[oldest].block, BLOCKSIZE);
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = ext2_time();
	DC[oldest].dirty = 0;
	spin_unlock(&lock);
}

void ext2_ramdisk_write_block(uint32_t block_no, uint8_t *buf) {
	if (!block_no) {
		kprintf("[kernel/ext2] block_no = 0?\n");
		kprintf("[kernel/ext2] Investigate the call before this, you have done something terrible!\n");
		return;
	}
	spin_lock(&lock);

	// update the cache
	int oldest = -1;
	uint32_t oldest_age = UINT32_MAX;
	for (uint32_t i = 0; i < CACHEENTRIES; ++i) {
		if (DC[i].block_no == block_no) {
			DC[i].last_use = ext2_time();
			DC[i].dirty = 1;
			memcpy(&DC[i].block, buf, BLOCKSIZE);
			spin_unlock(&lock);
			return;
		}
		if (DC[i].last_use < oldest_age) {
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}
	if (DC[oldest].dirty) {
		ext2_flush_dirty(oldest);
	}
	memcpy(&DC[oldest].block, buf, BLOCKSIZE);
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = ext2_time();
	DC[oldest].dirty = 1;
	spin_unlock(&lock);
}

static void ext2_set_real_block(ext2_inodetable_t *inode, uint32_t block, uint32_t real) {
	if (block < 12) {
		inode->block[block] = real;
		return;
	} else if (block < 12 + BLOCKSIZE / sizeof(uint32_t)) {
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[12], tmp);
		((uint32_t *)tmp)[block - 12] = real;
		ext2_ramdisk_write_block(inode->block[12], tmp);
		free(tmp);
		return;
	} else if (block < 12 + 256 + 256 * 256) {
		uint32_t a = block - 12;
		uint32_t b = a - 256;
		uint32_t c = b / 256;
		uint32_t d = b - c * 256;
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[13], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[c];
		ext2_ramdisk_read_block(nblock, tmp);
		((uint32_t *)tmp)[d] = real;
		ext2_ramdisk_write_block(nblock, tmp);
		free(tmp);
		return;
	} else if (block < 12 + 256 + 256 * 256 + 256 * 256 * 256) {
		uint32_t a = block - 12;
		uint32_t b = a - 256;
		uint32_t c = b - 256 * 256;
		uint32_t d = c / (256 * 256);
		uint32_t e = c - d * 256 * 256;
		uint32_t f = e / 256;
		uint32_t g = e - f * 256;
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[14], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[d];
		ext2_ramdisk_read_block(nblock, tmp);
		nblock = ((uint32_t *)tmp)[f];
		ext2_ramdisk_read_block(nblock, tmp);
		((uint32_t *)tmp)[g] = nblock;
		ext2_ramdisk_write_block(nblock, tmp);
		free(tmp);
		return;
	}

	HALT_AND_CATCH_FIRE("Attempted to set a file block that was too high :(", NULL);
}
/**
 * Return the actual block number represented by the 'block'th block 
 * in the 'inode'.
 */
static uint32_t ext2_get_real_block(ext2_inodetable_t *inode, uint32_t block) {
	if (block < 12) {
		return inode->block[block];
	} else if (block < 12 + BLOCKSIZE / sizeof(uint32_t)) {
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[12], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[block - 12];
		free(tmp);
		return nblock;
	} else if (block < 12 + 256 + 256 * 256) {
		uint32_t a = block - 12;
		uint32_t b = a - 256;
		uint32_t c = b / 256;
		uint32_t d = b - c * 256;
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[13], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[c];
		ext2_ramdisk_read_block(nblock, tmp);
		nblock = ((uint32_t *)tmp)[d];
		free(tmp);
		return nblock;
	} else if (block < 12 + 256 + 256 * 256 + 256 * 256 * 256) {
		uint32_t a = block - 12;
		uint32_t b = a - 256;
		uint32_t c = b - 256 * 256;
		uint32_t d = c / (256 * 256);
		uint32_t e = c - d * 256 * 256;
		uint32_t f = e / 256;
		uint32_t g = e - f * 256;
		uint8_t *tmp = malloc(BLOCKSIZE);
		ext2_ramdisk_read_block(inode->block[14], tmp);
		uint32_t nblock = ((uint32_t *)tmp)[d];
		ext2_ramdisk_read_block(nblock, tmp);
		nblock = ((uint32_t *)tmp)[f];
		ext2_ramdisk_read_block(nblock, tmp);
		nblock = ((uint32_t *)tmp)[g];
		free(tmp);
		return nblock;
	}

	HALT_AND_CATCH_FIRE("Attempted to get a file block that was too high :(", NULL);
	return 0;
}

/**
 * Allocate memory for a block in an inode whose inode number is 'no'.
 */
void ext2_ramdisk_inode_alloc_block(ext2_inodetable_t *inode, uint32_t inode_no, uint32_t block) {
	kprintf("Allocating block %d for inode #%d\n", block, inode_no);
	uint32_t block_no = 0, block_offset = 0, group = 0;
	char *bg_buffer = malloc(BLOCKSIZE);
	for (uint32_t i = 0; i < BGDS; ++i) {
		if (BGD[i].free_blocks_count > 0) {
			ext2_ramdisk_read_block(BGD[i].block_bitmap, (uint8_t *)bg_buffer);
			while (BLOCKBIT(block_offset))
				++block_offset;
			block_no = block_offset + SB->blocks_per_group * i + 1;
			group = i;
			break;
		}
	}
	if (!block_no) {
		kprintf("[kernel/ext2] No available blocks!\n");
		free(bg_buffer);
		return;
	}

	// Found a block (block_no), we need to mark it as in-use
	uint8_t b = BLOCKBYTE(block_offset);
	b |= SETBIT(block_offset);
	BLOCKBYTE(block_offset) = b;
	ext2_ramdisk_write_block(BGD[group].block_bitmap, (uint8_t *)bg_buffer);
	free(bg_buffer);

	ext2_set_real_block(inode, block, block_no);

	// Now update available blocks count
	BGD[group].free_blocks_count -= 1;
	ext2_ramdisk_write_block(2, (uint8_t *)BGD);

	inode->blocks++;
	ext2_ramdisk_write_inode(inode, inode_no);
}

/**
 * Read the 'block'th block within an inode 'inode', and put it into
 * the buffer 'buf'. In other words, this function reads the actual file
 * content. 'no' is the inode number of 'inode'.
 * @return the actual block number read from.
 */
uint32_t ext2_ramdisk_inode_read_block(ext2_inodetable_t *inode, uint32_t no, uint32_t block, uint8_t *buf) {
	// if the memory for 'block'th block has not been allocated to this inode, we need to 
	// allocate the memory first using block bitmap.
	if (block >= inode->blocks) {
		/* Invalid block requested, return 0s */
		memset(buf, 0x00, BLOCKSIZE);
		kprintf("[kernel/ext2] An invalid inode block [%d] was requested [have %d]\n", block, inode->blocks);
		return 0;
	}

	// The real work to read a block from an inode.
	uint32_t real_block = ext2_get_real_block(inode, block);
	ext2_ramdisk_read_block(real_block, buf);
	return real_block;
}

/**
 * Write to the 'block'th block within an inode 'inode' from the buffer 'buf'. 
 * In other words, this function writes to the actual file content.
 * @return the actual block number read from.
 */
uint32_t ext2_ramdisk_inode_write_block(ext2_inodetable_t *inode, uint32_t inode_no, uint32_t block, uint8_t *buf) {
	/* We must allocate blocks up to this point to account for unused space in the middle. */
	while (block >= inode->blocks) {
		ext2_ramdisk_inode_alloc_block(inode, inode_no, inode->blocks);
		if (block != inode->blocks - 1) {
			/* Clear the block */
			uint32_t real_block = ext2_get_real_block(inode, inode->blocks - 1);
			uint8_t empty[1024] = {0};
			memset(empty, 0x00, 1024);
			ext2_ramdisk_write_block(real_block, empty);
		}
	}

	// The real work to write to a block of an inode.
	uint32_t real_block = ext2_get_real_block(inode, block);

	kprintf("Virtual block %d maps to real block %d.\n", block, real_block);

	ext2_ramdisk_write_block(real_block, buf);
	return real_block;
}

/**
 * Create a new, regular, and empty file under directory 'parent'.
 */
static void ext2_create(fs_node_t *parent, char *name, uint16_t permission) {
	
	kprintf("[kernel/ext2] Creating file.\n");
	uint16_t mode = permission | EXT2_S_IFREG;
	ext2_inodetable_t *parent_inode = ext2_ramdisk_inode(parent->inode);
	
	// Check to make sure no same name in the parent dir
	fs_node_t *b_exist = finddir_ext2_ramdisk(parent, name);
	if (b_exist) {
		kprintf("[kernel/ext2] %s: Already exists\n", name);
		free(b_exist);
		free(parent_inode);
		return;
	}
	free(b_exist);

	// Create the inode under 'parent'
	uint32_t inode_no;
	ext2_inodetable_t *inode = ext2_ramdisk_alloc_inode(parent_inode, parent->inode, name, mode, &inode_no);
	free(parent_inode);

	if (inode == NULL) {
		kprintf("[kernel/ext2] Failed to create file '%s' (inode allocation failed)?\n", name);
		return;
	}

	free(inode);
}

/**
 * Make a new directory. 'name' consists the name for the new directory
 * to be created using 'permission' under 'parent' directory.
 * Message will be displayed in the terminal for success or failure.
 */
static void ext2_mkdir(fs_node_t *parent, char *name, uint16_t permission) {

	uint16_t mode = permission | EXT2_S_IFDIR;
	ext2_inodetable_t *parent_inode = ext2_ramdisk_inode(parent->inode);

	// Check to make sure no same name in the parent dir
	fs_node_t *b_exist = finddir_ext2_ramdisk(parent, name);
	if (b_exist) {
		kprintf("mkdir: %s: Already exists\n", name);
		free(b_exist);
		free(parent_inode);
		return;
	}
	free(b_exist);

	// Create the inode under 'parent'
	uint32_t inode_no;
	ext2_inodetable_t *inode = ext2_ramdisk_alloc_inode(parent_inode, parent->inode, name, mode, &inode_no);
	free(parent_inode);

	if (inode == NULL) {
		kprintf("mkdir: %s: Cannot be created\n", name);
		return;
	}

	// Init this newly created dir, put '.' and '..' into it.
	// Here we pass in 0 as the inode number for '.' and '..' because the 
	// 'cd' command can handle them correctly, so it does not matter.
	insertdir_ext2_ramdisk(inode, inode_no, 0, ".", 2);
	insertdir_ext2_ramdisk(inode, inode_no, 0, "..", 2);

	free(inode);
}

static uint8_t mode_to_filetype(uint16_t mode) {
	uint16_t ftype = mode & 0xF000;
	switch (ftype) {
		case EXT2_S_IFREG:
			return 1;
		case EXT2_S_IFDIR:
			return 2;
		case EXT2_S_IFCHR:
			return 3;
		case EXT2_S_IFBLK:
			return 4;
		case EXT2_S_IFIFO:
			return 5;
		case EXT2_S_IFSOCK:
			return 6;
		case EXT2_S_IFLNK:
			return 7;
	}

	// File type is unknown
	return 0;
}

/**
 * Allocate a new inode with parent as the parent directory node and name as the filename
 * within that parent directory. Returns a pointer to a memory-copy of the node which
 * the client can (and should) free.
 * 'ftype' is file type, used when adding the entry to the parent dir. 1 for regular file,
 * 2 for directory, etc... 'no' is the inode number of 'parent'.
 * Upon return, the inode number of the newly allocated inode will be stored in 'inode_no'.
 * 
 * This function assumes that parent directory 'parent' does not contain any entry with 
 * same name as 'name'. Caller shuold ensure this.
 * Note that inode just created using this function has size of 0, which means no data 
 * blocks have been allocated to the inode.
 */
ext2_inodetable_t *ext2_ramdisk_alloc_inode
(
	ext2_inodetable_t *parent, 
	uint32_t no,
	char *name, 
	uint16_t mode, 
    uint32_t *inode_no	
) {
	if ((parent->mode & EXT2_S_IFDIR) == 0 || name == NULL) {
		kprintf("[kernel/ext2] No name or bad parent.\n");
		return NULL;
	}
	
	ext2_inodetable_t *inode;

	uint32_t node_no = 0, node_offset = 0, group = 0;
	char *bg_buffer = malloc(BLOCKSIZE);
	/* Locate a block with an available inode. Will probably be the first block group. */
	for (uint32_t i = 0; i < BGDS; ++i) {
		if (BGD[i].free_inodes_count > 0) {
#if EXT2_DEBUG_BLOCK_DESCRIPTORS
			kprintf("Group %d has %d free inodes!\n", i, BGD[i].free_inodes_count);
#endif
			ext2_ramdisk_read_block(BGD[i].inode_bitmap, (uint8_t *)bg_buffer);
			while (BLOCKBIT(node_offset))
				++node_offset;
			node_no = node_offset + ext2_ramdisk_inodes_per_group * i + 1;
			group = i;
			break;
		}
	}
	if (!node_no) {
		kprintf("[kernel/ext2] Failure: No free inodes in block descriptors!\n");
		free(bg_buffer);
		return NULL;
	}
	/* Alright, we found an inode (node_no), we need to mark it as in-use... */
	uint8_t b = BLOCKBYTE(node_offset);
#if EXT2_DEBUG_BLOCK_DESCRIPTORS
	kprintf("Located an inode at #%d (%d), the byte for this block is currently set to %x\n", node_no, node_offset, (uint32_t)b);
#endif
	b |= SETBIT(node_offset);
#if EXT2_DEBUG_BLOCK_DESCRIPTORS	
	kprintf("We would want to set it to %x\n", (uint32_t)b);
	kprintf("Setting it in our temporary buffer...\n");
#endif
	BLOCKBYTE(node_offset) = b;
#if EXT2_DEBUG_BLOCK_DESCRIPTORS	
	kprintf("\nWriting back out.\n");
#endif
	ext2_ramdisk_write_block(BGD[group].inode_bitmap, (uint8_t *)bg_buffer);
	free(bg_buffer);
#if EXT2_DEBUG_BLOCK_DESCRIPTORS	
	kprintf("Okay, now we need to update the available inodes count...\n");
	kprintf("it is %d, it should be %d\n", BGD[group].free_inodes_count, BGD[group].free_inodes_count - 1);
	kprintf("\n");
	kprintf("%d\n", BGD[group].free_inodes_count);
#endif
	BGD[group].free_inodes_count -= 1;
#if EXT2_DEBUG_BLOCK_DESCRIPTORS	
	kprintf("%d\n", BGD[group].free_inodes_count);
	kprintf("\nOkay, writing the block descriptors back to ramdisk.\n");
#endif	
	ext2_ramdisk_write_block(2, (uint8_t *)BGD);
	
#if EXT2_DEBUG_BLOCK_DESCRIPTORS	
	kprintf("Alright, we have an inode (%d), time to write it out to ramdisk and make the file in the directory.\n", node_no);
#endif

	// Get the inode struct from the ramdisk and init it
	inode = ext2_ramdisk_inode(node_no);
	inode->size = 0;
	inode->blocks = 0;
	inode->mode = mode;
	ext2_ramdisk_write_inode(inode, node_no);
	*inode_no = node_no;

	// Create an entry in the parent directory
	uint8_t ftype = mode_to_filetype(mode);
	kprintf("[kernel/ext2] Allocated inode, inserting directory entry [%d]...\n", node_no);
	insertdir_ext2_ramdisk(parent, no, node_no, name, ftype);

	return inode;
}

/**
 * Return the 'index'th directory entry in the directory represented by 'inode'.
 * Caller should free the memory.
 */
ext2_dir_t *ext2_ramdisk_direntry(ext2_inodetable_t *inode, uint32_t no, uint32_t index) {
	uint8_t *block = malloc(BLOCKSIZE);
	uint8_t block_nr = 0;
	ext2_ramdisk_inode_read_block(inode, no, block_nr, block);
	uint32_t dir_offset = 0;
	uint32_t total_offset = 0;
	uint32_t dir_index = 0;
	
	while (total_offset < inode->size && dir_index <= index) {
		ext2_dir_t *d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
	
		if (dir_index == index) {
			ext2_dir_t *out = malloc(d_ent->rec_len);
			memcpy(out, d_ent, d_ent->rec_len);
			free(block);
			return out;
		}

		dir_offset += d_ent->rec_len;
		total_offset += d_ent->rec_len;
		dir_index++;

		// move on to the next block of this directory if needed.
		if (dir_offset >= BLOCKSIZE) {
			block_nr++;
			dir_offset -= BLOCKSIZE;
			ext2_ramdisk_inode_read_block(inode, no, block_nr, block);
		}
	}
	
	free(block);
	return NULL;
}

/**
 * Return the inode object on ramdisk representing 'inode'th inode.
 * Caller should free the memory.
 */
ext2_inodetable_t *ext2_ramdisk_inode(uint32_t inode) {
	uint32_t group = inode / ext2_ramdisk_inodes_per_group;
	if (group > BGDS) {
		return NULL;
	}
	uint32_t inode_table_block = BGD[group].inode_table;
	inode -= group * ext2_ramdisk_inodes_per_group;	// adjust index within group
	uint32_t block_offset		= ((inode - 1) * SB->inode_size) / BLOCKSIZE;
	uint32_t offset_in_block    = (inode - 1) - block_offset * (BLOCKSIZE / SB->inode_size);
	uint8_t *buf                = malloc(BLOCKSIZE);
	ext2_inodetable_t *inodet   = malloc(BLOCKSIZE);
 
	ext2_ramdisk_read_block(inode_table_block + block_offset, buf);
	ext2_inodetable_t *inodes = (ext2_inodetable_t *)buf;
	memcpy(inodet, &inodes[offset_in_block], sizeof(ext2_inodetable_t));

	free(buf);
	return inodet;
}

/**
 * Write the 'inode' into the inode table at position 'index'.
 */
void ext2_ramdisk_write_inode(ext2_inodetable_t *inode, uint32_t index) {
	uint32_t group = index / ext2_ramdisk_inodes_per_group;
	if (group > BGDS) {
		return;
	}
	
	uint32_t inode_table_block = BGD[group].inode_table;
	index -= group * ext2_ramdisk_inodes_per_group;	// adjust index within group
	uint32_t block_offset = ((index - 1) * SB->inode_size) / BLOCKSIZE;
	uint32_t offset_in_block = (index - 1) - block_offset * (BLOCKSIZE / SB->inode_size);

	ext2_inodetable_t *inodet = malloc(BLOCKSIZE);
	/* Read the current table block */
	ext2_ramdisk_read_block(inode_table_block + block_offset, (uint8_t *)inodet);
	memcpy(&inodet[offset_in_block], inode, sizeof(ext2_inodetable_t));
	ext2_ramdisk_write_block(inode_table_block + block_offset, (uint8_t *)inodet);
	free(inodet);
}

uint32_t write_ext2_ramdisk(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	ext2_inodetable_t *inode = ext2_ramdisk_inode(node->inode);
	uint32_t end = offset + size;
	uint32_t start_block 	= offset / BLOCKSIZE;
	uint32_t end_block		= end / BLOCKSIZE;
	uint32_t end_size		= end - end_block * BLOCKSIZE;
	uint32_t size_to_write  = end - offset;
	kprintf("[kernel/ext2] Write at node 0x%x, offset %d, size %d, buffer=0x%x\n", node, offset, size, buffer);
	if (end_size == 0) {
		end_block--;
	}
	
	// need to update if size has increased.
	if (inode->size < end) {
		inode->size = end;
		ext2_ramdisk_write_inode(inode, node->inode);
	}
	
	if (start_block == end_block) {
		void *buf = malloc(BLOCKSIZE);
		ext2_ramdisk_inode_read_block(inode, node->inode, start_block, buf);
		memcpy((uint8_t *)((uintptr_t)buf + (offset % BLOCKSIZE)), buffer, size_to_write);
		kprintf("[kernel/ext2] Single-block write.\n");
		ext2_ramdisk_inode_write_block(inode, node->inode, start_block, buf);
		free(buf);
		free(inode);
		return size_to_write;
	} else {
		uint32_t block_offset;
		uint32_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = malloc(BLOCKSIZE);
				ext2_ramdisk_inode_read_block(inode, node->inode, block_offset, buf);
				memcpy((uint8_t *)((uint32_t)buf + (offset % BLOCKSIZE)), buffer, BLOCKSIZE - (offset % BLOCKSIZE));
				kprintf("[kernel/ext2] Writing block [loop...]...\n");
				ext2_ramdisk_inode_write_block(inode, node->inode, start_block, buf);
				free(buf);
			} else {
				kprintf("[kernel/ext2] Writing block [buffer...?]...\n");
				ext2_ramdisk_inode_write_block(inode, node->inode, block_offset, 
						buffer + BLOCKSIZE * blocks_read - (block_offset % BLOCKSIZE));
			}
		}
		void *buf = malloc(BLOCKSIZE);
		ext2_ramdisk_inode_read_block(inode, node->inode, end_block, buf);
		memcpy(buf, buffer + BLOCKSIZE * blocks_read - (block_offset % BLOCKSIZE), end_size);
		kprintf("[kernel/ext2] Writing block [tail]...\n");
		ext2_ramdisk_inode_write_block(inode, node->inode, end_block, buf);
		free(buf);
	}
	free(inode);
	return size_to_write;
}

uint32_t read_ext2_ramdisk(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	ext2_inodetable_t *inode = ext2_ramdisk_inode(node->inode);
	uint32_t end;
	if (offset + size > inode->size) {
		end = inode->size;
	} else {
		end = offset + size;
	}
	uint32_t start_block  = offset / BLOCKSIZE;
	uint32_t end_block    = end / BLOCKSIZE;
	uint32_t end_size     = end - end_block * BLOCKSIZE;
	uint32_t size_to_read = end - offset;
	if (end_size == 0) {
		end_block--;
	}
	if (start_block == end_block) {
		void *buf = malloc(BLOCKSIZE);
		ext2_ramdisk_inode_read_block(inode, node->inode, start_block, buf);
		memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), size_to_read);
		free(buf);
		free(inode);
		return size_to_read;
	} else {
		uint32_t block_offset;
		uint32_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = malloc(BLOCKSIZE);
				ext2_ramdisk_inode_read_block(inode, node->inode, block_offset, buf);
				memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), BLOCKSIZE - (offset % BLOCKSIZE));
				free(buf);
			} else {
				void *buf = malloc(BLOCKSIZE);
				ext2_ramdisk_inode_read_block(inode, node->inode, block_offset, buf);
				memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, BLOCKSIZE);
				free(buf);
			}
		}
		void *buf = malloc(BLOCKSIZE);
		ext2_ramdisk_inode_read_block(inode, node->inode, end_block, buf);
		memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, end_size);
		free(buf);
	}
	free(inode);
	return size_to_read;
}

void
open_ext2_ramdisk(fs_node_t *node, uint8_t read, uint8_t write) {
	/* Nothing to do here */
}

void
close_ext2_ramdisk(fs_node_t *node) {
	/* Nothing to do here */
}

/**
 * Return the 'index'th entry in the directory 'node'.
 * Caller should free the memory.
 */
struct dirent *
readdir_ext2_ramdisk(fs_node_t *node, uint32_t index) {
	
	ext2_inodetable_t *inode = ext2_ramdisk_inode(node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	ext2_dir_t *direntry = ext2_ramdisk_direntry(inode, node->inode, index);
	if (!direntry) {
		return NULL;
	}
	struct dirent *dirent = malloc(sizeof(struct dirent));
	memcpy(&dirent->name, &direntry->name, direntry->name_len);
	dirent->name[direntry->name_len] = '\0';
	dirent->ino = direntry->inode;
	free(direntry);
	free(inode);
	return dirent;
}

/**
 * Insert an entry named 'name' with type 'type' into a directory 'p_node' 
 * at the end.
 * This function assumes that parent directory 'p_node' does not contain
 * any entry with same name as 'name'. Caller should ensure this.
 */
void insertdir_ext2_ramdisk(ext2_inodetable_t *p_node, uint32_t no, uint32_t inode, char *name, uint8_t type) {
	/* XXX HACK This needs to be seriously fixed up. */
	kprintf("[kernel/ext2] Request to insert new directory entry at 0x%x#%d->%d '%s' type %d\n", p_node, no, inode, name, type);
	assert(p_node->mode & EXT2_S_IFDIR);
	void *block = malloc(BLOCKSIZE);
	uint32_t block_nr = 0;
	ext2_ramdisk_inode_read_block(p_node, no, block_nr, block);
	uint32_t dir_offset = 0;
	uint32_t total_offset = 0;

	// first, iterate pass the last entry in the parent directory.
	while (total_offset < p_node->size) {
		ext2_dir_t *d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		if (d_ent->rec_len + total_offset == p_node->size) {
			d_ent->rec_len = d_ent->name_len + sizeof(ext2_dir_t);
			while (d_ent->rec_len % 4 > 0) {
				d_ent->rec_len++;
			}
			dir_offset   += d_ent->rec_len;
			total_offset += d_ent->rec_len;
			break;
		}

		dir_offset += d_ent->rec_len;
		total_offset += d_ent->rec_len;

		// move on to the next block of this directory if needed.
		if (dir_offset >= BLOCKSIZE) {
			block_nr++;
			dir_offset -= BLOCKSIZE;
			ext2_ramdisk_inode_read_block(p_node, no, block_nr, block);
			kprintf("[kernel/ext2] Advancing to next block...\n");
		}
	}

	kprintf("[kernel/ext2] Total Offset = %d; block = %d; offset within block = %d\n", total_offset, block_nr, dir_offset);

	// Put the new directory entry at 'dir_offset' in block 'block_nr'.
	uint32_t size = p_node->size - total_offset;
	if (dir_offset + size > BLOCKSIZE) {
		kprintf("\033[1;31m[kernel/ext2] Just a warning: You probably just fucked everything.\033[0m\n");
	}
	ext2_dir_t *new_entry = malloc(size);

	// Initialize the new entry.
	new_entry->inode = inode;
	new_entry->rec_len = size;
	new_entry->name_len = (uint8_t)strlen(name);
	new_entry->file_type = type;
	memcpy(&new_entry->name, name, strlen(name));

	// Write back to block.
	memcpy(((uint8_t *)block) + dir_offset, new_entry, size);
	memset(((uint8_t *)block) + dir_offset + new_entry->rec_len, 0x00, 4);
	ext2_ramdisk_inode_write_block(p_node, no, block_nr, block);

	free(new_entry);

	// Update parent node size
	//p_node->size += size;
	ext2_ramdisk_write_inode(p_node, no);

	free(block);
}

/**
 * Find the actual inode in the ramramdisk image for the requested file.
 */
fs_node_t *finddir_ext2_ramdisk(fs_node_t *node, char *name) {
	
	ext2_inodetable_t *inode = ext2_ramdisk_inode(node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	void *block = malloc(BLOCKSIZE);
	ext2_dir_t *direntry = NULL;
	uint8_t block_nr = 0;
	ext2_ramdisk_inode_read_block(inode, node->inode, block_nr, block);
	uint32_t dir_offset = 0;
	uint32_t total_offset = 0;

	// Look through the requested entries until we find what we're looking for
#if EXT2_DEBUG_BLOCK_DESCRIPTORS
	kprintf("file looking for: %s\n", name);
	kprintf("total size: %d\n", inode->size);
#endif
	while (total_offset < inode->size) {
#if EXT2_DEBUG_BLOCK_DESCRIPTORS
		kprintf("dir_offset: %d\n", dir_offset);
		kprintf("total_offset: %d\n", total_offset);
#endif
		ext2_dir_t *d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		
		if (strlen(name) != d_ent->name_len) {
			dir_offset += d_ent->rec_len;
			total_offset += d_ent->rec_len;

			// move on to the next block of this directory if need.
			if (dir_offset >= BLOCKSIZE) {
				block_nr++;
				dir_offset -= BLOCKSIZE;
				ext2_ramdisk_inode_read_block(inode, node->inode, block_nr, block);
			}

			continue;
		}

		char *dname = malloc(sizeof(char) * (d_ent->name_len + 1));
		memcpy(dname, &(d_ent->name), d_ent->name_len);
		dname[d_ent->name_len] = '\0';
#if EXT2_DEBUG_BLOCK_DESCRIPTORS
		kprintf("cur file: %s\n", dname);
#endif
		if (!strcmp(dname, name)) {
			free(dname);
			direntry = malloc(d_ent->rec_len);
			memcpy(direntry, d_ent, d_ent->rec_len);
			break;
		}
		free(dname);

		dir_offset += d_ent->rec_len;
		total_offset += d_ent->rec_len;

		// move on to the next block of this directory if need.
		if (dir_offset >= BLOCKSIZE) {
			block_nr++;
			dir_offset -= BLOCKSIZE;
			ext2_ramdisk_inode_read_block(inode, node->inode, block_nr, block);
		}
	}
	free(inode);
	free(block);
	if (!direntry) {
		// We could not find the requested entry in this directory.
		return NULL;
	}
	fs_node_t *outnode = malloc(sizeof(fs_node_t));
	inode = ext2_ramdisk_inode(direntry->inode);
	ext2_ramdisk_node_from_file(inode, direntry, outnode);
	free(direntry);
	free(inode);
	return outnode;
}

/**
 * Initialize in-memory struct 'fnode' using on-ramdisk structs 'inode' and 'direntry'.
 */
uint32_t ext2_ramdisk_node_from_file(ext2_inodetable_t *inode, ext2_dir_t *direntry, 
								  fs_node_t *fnode) {
	if (!fnode) {
		/* You didn't give me a node to write into, go **** yourself */
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
		fnode->create = NULL;
		fnode->mkdir = NULL;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
		fnode->create = ext2_create;
		fnode->mkdir = ext2_mkdir;
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
	fnode->read    = read_ext2_ramdisk;
	fnode->write   = write_ext2_ramdisk;
	fnode->open    = open_ext2_ramdisk;
	fnode->close   = close_ext2_ramdisk;
	fnode->readdir = readdir_ext2_ramdisk;
	fnode->finddir = finddir_ext2_ramdisk;
	return 1;
}

/**
 * Intiailize in-memory struct 'fnode' that represents "/" using 'inode'.
 */
uint32_t ext2_ramdisk_node_root(ext2_inodetable_t *inode, fs_node_t *fnode) {
	if (!fnode) {
		return 0;
	}
	/* Information for root dir */
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
		fnode->create = NULL;
		fnode->mkdir = NULL;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
		fnode->create = ext2_create;
		fnode->mkdir = ext2_mkdir;
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
	fnode->read    = read_ext2_ramdisk;
	fnode->write   = write_ext2_ramdisk;
	fnode->open    = open_ext2_ramdisk;
	fnode->close   = close_ext2_ramdisk;
	fnode->readdir = readdir_ext2_ramdisk;
	fnode->finddir = finddir_ext2_ramdisk;
	return 1;
}

void ext2_ramdisk_read_superblock() {
	kprintf("Volume '%s'\n", SB->volume_name);
	kprintf("%d inodes\n", SB->inodes_count);
	kprintf("%d blocks\n", SB->blocks_count);
	kprintf("%d free blocks\n", SB->free_blocks_count);
	kprintf("0x%x last mount time\n", SB->mtime);
	kprintf("0x%x last write time\n", SB->wtime);
	kprintf("Mounted %d times.\n", SB->mnt_count);
	kprintf("0x%x\n", SB->magic);
}

void ext2_ramdisk_sync() {
	spin_lock(&lock);
	for (uint32_t i = 0; i < CACHEENTRIES; ++i) {
		if (DC[i].dirty) {
			ext2_flush_dirty(i);
		}
	}
	spin_unlock(&lock);
}

void ext2_ramdisk_mount(uint32_t offset) {
	_ramdisk_offset = offset;
	DC = malloc(sizeof(ext2_ramdisk_cache_entry_t) * CACHEENTRIES);
	SB = malloc(BLOCKSIZE);
	ext2_ramdisk_read_block(1, (uint8_t *)SB);
	assert(SB->magic == EXT2_SUPER_MAGIC);
	if (SB->inode_size == 0) {
		SB->inode_size = 128;
	}
	BGDS = SB->blocks_count / SB->blocks_per_group;
	ext2_ramdisk_inodes_per_group = SB->inodes_count / BGDS;

	// load the block group descriptors
	BGD = malloc(BLOCKSIZE);
	ext2_ramdisk_read_block(2, (uint8_t *)BGD);

#if EXT2_DEBUG_BLOCK_DESCRIPTORS
	char bg_buffer[BLOCKSIZE];
	for (uint32_t i = 0; i < BGDS; ++i) {
		kprintf("Block Group Descriptor #%d @ %d\n", i, 2 + i * SB->blocks_per_group);
		kprintf("\tBlock Bitmap @ %d\n", BGD[i].block_bitmap); { 
			kprintf("\t\tExamining block bitmap at %d\n", BGD[i].block_bitmap);
			ext2_ramdisk_read_block(BGD[i].block_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			kprintf("\t\tFirst free block in group is %d\n", j + BGD[i].block_bitmap - 2);
		}
		kprintf("\tInode Bitmap @ %d\n", BGD[i].inode_bitmap); {
			kprintf("\t\tExamining inode bitmap at %d\n", BGD[i].inode_bitmap);
			ext2_ramdisk_read_block(BGD[i].inode_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			kprintf("\t\tFirst free inode in group is %d\n", j + ext2_ramdisk_inodes_per_group * i + 1);
		}
		kprintf("\tInode Table  @ %d\n", BGD[i].inode_table);
		kprintf("\tFree Blocks =  %d\n", BGD[i].free_blocks_count);
		kprintf("\tFree Inodes =  %d\n", BGD[i].free_inodes_count);
	}
#endif
	ext2_inodetable_t *root_inode = ext2_ramdisk_inode(2);

	RN = (fs_node_t *)malloc(sizeof(fs_node_t));
	assert(ext2_ramdisk_node_root(root_inode, RN));
	fs_root = RN;
	LOG(INFO,"Mounted EXT2 ramdisk, root VFS node is at 0x%x", RN);
}

void ext2_ramdisk_forget_superblock() {
	free(SB);
}

