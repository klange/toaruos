#include <system.h>
#include <types.h>
#include <fs.h>
#include <ext2.h>
#include <logging.h>
#include <module.h>

#define EXT2_BGD_BLOCK 2

#define E_SUCCESS   0
#define E_BADBLOCK  1
#define E_NOSPACE   2
#define E_BADPARENT 3

#if 0

/*
 * EXT2 filesystem object
 */
typedef struct {
	ext2_superblock_t       * superblock;          /* Device superblock, contains important information */
	ext2_bgdescriptor_t     * block_groups;        /* Block Group Descriptor / Block groups */
	fs_node_t               * root_node;           /* Root FS node (attached to mountpoint) */

	fs_node_t               * block_device;        /* Block device node XXX unused */
	unsigned int              device_offset;       /* Offset into the block device where we start */

	unsigned int              block_size;          /* Size of one block */
	unsigned int              pointers_per_block;  /* Number of pointers that fit in a block */
	unsigned int              sector_size;         /* Size of one sector */
	unsigned int              inodes_per_group;    /* Number of inodes in a "group" */
	unsigned int              block_group_count;   /* Number of blocks groups */

	ext2_disk_cache_entry_t * disk_cache;          /* Dynamically allocated array of cache entries */
	unsigned int              cache_entries;       /* Size of ->disk_cache */
	unsigned int              cache_time;          /* "timer" that increments with each cache read/write */

	uint8_t volatile          lock;                /* Synchronization lock point */
} ext2_fs_t;

/*
 * These macros were used in the original toaru ext2 driver.
 * They make referring to some of the core parts of the drive a bit easier.
 */
#define BGDS (this->block_groups)
#define SB   (this->superblock)
#define BGD  (this->block_groups)
#define RN   (this->root_node)
#define DC   (this->disk_cache)

/*
 * These macros deal with the block group descriptor bitmap
 */
#define BLOCKBIT(n)  (bg_buffer[((n) >> 3)] & (1 << (((n) % 8))))
#define BLOCKBYTE(n) (bg_buffer[((n) >> 3)])
#define SETBIT(n)    (1 << (((n) % 8)))

/*
 * XXX ATA disk port (block device this shit!)
 */
#define DISK_PORT 0x1F0

*
/**
 * ext2->block_to_sector Convert a block number to a sector offset
 *
 * @param block Block number to convert.
 * @returns Corresponding sector ofset
 */
static unsigned int block_to_sector(ext2_fs_t * this, unsigned int block) {
	return this->device_offset + block * (this->block_size / this->sector_size);
}

/**
 * ext2->get_cache_time Increment and return the current cache time
 *
 * @returns Current cache time
 */
static unsigned int get_cache_time(ext2_fs_t * this) {
	return this->cache_time++;
}

/**
 * ext2->cache_flush_dirty Flush dirty cache entry to the disk.
 *
 * @param ent_no Cache entry to dump
 * @returns Error code or E_SUCCESS
 */
static int cache_flush_dirty(ext2_fs_t * this, unsigned int ent_no) {
	for (uint32_t i = 0; i < this->block_size / this->sector_size; ++i) {
		ide_write_sector_retry(DISK_PORT, 0, block_to_sector(this, DC[ent_no].block_no) + i,
				(uint8_t *)((uintptr_t)DC[ent_no].block + this->sector_size * i));
	}
	DC[ent_no].dirty = 0;

	return E_SUCCESS;
}

/**
 * ext2->read_block Read a block from the block device associated with this filesystem.
 *
 * The read block will be copied into the buffer pointed to by `buf`.
 *
 * @param block_no Number of block to read.
 * @param buf      Where to put the data read.
 * @returns Error code or E_SUCCESS
 */
static int read_block(ext2_fs_t * this, unsigned int block_no, uint8_t * buf) {
	/* 0 is an invalid block number. So is anything beyond the total block count, but we can't check that. */
	if (!block_no) {
		debug_print(ERROR, "Tried to read block #0 from ext2 file system. Enable tracing and retry this operation.");
		debug_print(ERROR, "If this was part of a write, your file system is most likely corrupted now.");
		return E_BADBLOCK;
	}

	/* This operation requires the filesystem lock to be obtained */
	spin_lock(&this->lock);

	/* We can make reads without a cache in place. */
	if (!DC) {
		/* In such cases, we read directly from the block device */
		for (unsigned int i = 0; i < this->block_size / this->sector_size; ++i) {
			/* XXX We are reading sectors from an ATA device; this should be a block device read! */
			ide_read_sector(DISK_PORT, 0, block_to_sector(this, block_no) + i,
					(uint8_t *)((uintptr_t)buf + this->sector_size * i));
		}
		/* We are done, release the lock */
		spin_unlock(&this->lock);
		/* And return SUCCESS */
		return E_SUCCESS;
	}

	/*
	 * Search the cache for this entry
	 * We'll look for the oldest entry, too.
	 */
	int oldest = -1;
	unsigned int oldest_age = UINT32_MAX;
	for (unsigned int i = 0; i < this->cache_entries; ++i) {
		if (DC[i].block_no == block_no) {
			/* We found it! Update usage times */
			DC[i].last_use = get_cache_time(this);
			/* Read the block */
			memcpy(buf, DC[i].block, this->block_size);
			/* Release the lock */
			spin_unlock(&this->lock);
			/* Success! */
			return E_SUCCESS;
		}
		if (DC[i].last_use < oldest_age) {
			/* We found an older block, remember this. */
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}

	/*
	 * At this point, we did not find this block in the cache.
	 * We are going to replace the oldest entry with this new one.
	 */

	/* We'll start by flushing the block if it was dirty. */
	if (DC[oldest].dirty) {
		cache_flush_dirty(this, oldest);
	}

	/* Then we'll read the new one */
	for (unsigned int i = 0; i < this->block_size / this->sector_size; ++i) {
		/* XXX block device read should go here */
		ide_read_sector(DISK_PORT, 0, block_to_sector(this, block_no) + i,
				(uint8_t *)((uintptr_t)(DC[oldest].block) + this->sector_size * i));
	}

	/* And copy the results to the output buffer */
	memcpy(buf, DC[oldest].block, this->block_size);

	/* And update the cache entry to point to the new block */
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = get_cache_time(this);
	DC[oldest].dirty = 0;

	/* Release the lock */
	spin_unlock(&this->lock);

	/* And return success */
	return E_SUCCESS;
}

/**
 * ext2->write_block Write a block to the block device.
 *
 * @param block_no Block to write
 * @param buf      Data in the block
 * @returns Error code or E_SUCESSS
 */
static int write_block(ext2_fs_t * this, unsigned int block_no, uint8_t *buf) {
	if (!block_no) {
		debug_print(ERROR, "Attempted to write to block #0. Enable tracing and retry this operation.");
		debug_print(ERROR, "Your file system is most likely corrupted now.");
		return E_BADBLOCK;
	}

	/* This operation requires the filesystem lock */
	spin_lock(&this->lock);

	/* Find the entry in the cache */
	int oldest = -1;
	unsigned int oldest_age = UINT32_MAX;
	for (unsigned int i = 0; i < this->cache_entries; ++i) {
		if (DC[i].block_no == block_no) {
			/* We found it. Update the cache entry */
			DC[i].last_use = get_cache_time(this);
			DC[i].dirty = 1;
			memcpy(DC[i].block, buf, this->block_size);
			spin_unlock(&this->lock);
			return E_SUCESS;
		}
		if (DC[i].last_use < oldest_age) {
			/* Keep track of the oldest entry */
			oldest = i;
			oldest_age = DC[i].last_use;
		}
	}

	/* We did not find this element in the cache, so make room. */
	if (DC[oldest].dirty) {
		/* Flush the oldest entry */
		cache_flush_dirty(this, oldest);
	}

	/* Update the entry */
	memcpy(DC[oldest].block, buf, this->block_size);
	DC[oldest].block_no = block_no;
	DC[oldest].last_use = get_cach_time(this);
	DC[oldest].dirty = 1;

	/* Release the lock */
	spin_unlock(&this->lock);

	/* We're done. */
	return E_SUCCESS;
}

/**
 * ext2->set_block_number Set the "real" block number for a given "inode" block number.
 *
 * @param inode   Inode to operate on
 * @param iblock  Block offset within the inode
 * @param rblock  Real block number
 * @returns Error code or E_SUCCESS
 */
static unsigned int set_block_number(ext2_fs_t * this, ext2_inodetable_t * inode, unsigned int iblock, unsigned int rblock) {

	unsigned int p = this->pointers_per_block;

	/* We're going to do some crazy math in a bit... */
	unsigned int a, b, c, d, e, f, g;

	if (iblock < EXT2_DIRECT_BLOCKS) {
		inode->block[iblock] = rblock;
		return E_SUCCESS;
	} else if (iblock < EXT2_DIRECT_BLOCKS + p) {
		/* XXX what if inode->block[EXT2_DIRECT_BLOCKS] isn't set? */
		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS], &tmp);

		((uint32_t *)&tmp)[iblock - EXT2_DIRECT_BLOCKS] = rblock;
		write_block(this, inode->block[EXT2_DIRECT_BLOCKS], &tmp);

		return E_SUCCESS;
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b / p;
		d = b - c * p;

		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 1], &tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[c];
		read_block(this, nblock, &tmp);

		((uint32_t  *)&tmp)[d] = rblock;
		write_block(this, nblock, &tmp);

		return E_SUCCESS;
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p + p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b - p * p;
		d = c / (p * p);
		e = c - d * p * p;
		f = e / p;
		g = e - f * p;

		uint8_t tmp[this->block_size];
		read_block(this, indoe->block[EXT2_DIRECT_BLOCKS + 2], &tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[d];
		read_block(this, nblock, &tmp);

		nblock = ((uint32_t *)&tmp)[f];
		read_block(this, nblock, &tmp);

		((uint32_t *)&tmp)[g] = nblock;
		write_block(this, nblock, &tmp);

		return E_SUCCESS;
	}

	debug_print(CRITICAL, "EXT2 driver tried to write to a block number that was too high (%d)", rblock);
	return E_BADBLOCK;
}

/**
 * ext2->get_block_number Given an inode block number, get the real block number.
 *
 * @param inode   Inode to operate on
 * @param iblock  Block offset within the inode
 * @returns Real block number
 */
static unsigned int get_block_number(ext2_fs_t * this, ext2_inodetable_t * inode, unsigned int iblock) {

	unsigned int p = this->pointers_per_block;

	/* We're going to do some crazy math in a bit... */
	unsigned int a, b, c, d, e, f, g;

	if (iblock < EXT2_DIRECT_BLOCKS) {
		return inode->block[iblock];
	} else if (iblock < EXT2_DIRECT_BLOCKS + p) {
		/* XXX what if inode->block[EXT2_DIRECT_BLOCKS] isn't set? */
		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS], &tmp);

		return ((uint32_t *)&tmp)[iblock - EXT2_DIRECT_BLOCKS];
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b / p;
		d = b - c * p;

		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 1], &tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[c];
		read_block(this, nblock, &tmp);

		return ((uint32_t  *)&tmp)[d] = rblock;
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p + p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b - p * p;
		d = c / (p * p);
		e = c - d * p * p;
		f = e / p;
		g = e - f * p;

		uint8_t tmp[this->block_size];
		read_block(this, indoe->block[EXT2_DIRECT_BLOCKS + 2], &tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[d];
		read_block(this, nblock, &tmp);

		nblock = ((uint32_t *)&tmp)[f];
		read_block(this, nblock, &tmp);

		return ((uint32_t *)&tmp)[g] = nblock;
	}

	debug_print(CRITICAL, "EXT2 driver tried to read to a block number that was too high (%d)", rblock);

	return 0;
}

/**
 * ext2->allocate_inode_block Allocate a block in an inode.
 *
 * @param inode Inode to operate on
 * @param inode_no Number of the inode (this is not part of the struct)
 * @param block Block within inode to allocate
 * @returns Error code or E_SUCCESS
 */
static int allocate_inode_block(ext2_fs_t * this, ext2_inodetable_t * inode, unsigned int inode_no, unsigned int block) {
	debug_print(NOTICE, "Allocating block #%d for inode #%d", block, inode_no);
	unsigned int block_no     = 0;
	unsigned int block_offset = 0;
	unsigned int group        = 0;
	uint8_t bg_buffer[this->block_size];

	for (unsigned int i = 0; i < BGDS; ++i) {
		if (BGD[i].free_blocks_count > 0) {
			read_block(this, BGD[i].block_bitmap, &bg_buffer);
			while (BLOCKBIT(block_offset)) {
				++block_offset;
			}
			block_no = block_offset + SB->blocks_per_group * i + 1;
			group = i;
			break;
		}
	}

	if (!block_no) {
		debug_print(CRITICAL, "No available blocks, disk is out of space!");
		return E_NOSPACE;
	}

	uint8_t b = BLOCKBYTE(block_offset);
	b |= SETBIT(block_offset);
	BLOCKBYTE(block_offset) = b;
	write_block(this, BGD[group].block_bitmap, &bg_buffer);

	set_block_number(this, inode, block, block_no);

	BGD[group].free_blocks_count--;
	write_block(this, BGD_BLOCK, (uint8_t *)BGD);

	inode->blocks++;
	write_inode(this, inode, inode_no);

	return E_SUCCESS;
}

/**
 * ext2->inode_read_block
 *
 * @param inode
 * @param no
 * @param block
 * @parma buf
 * @returns Real block number for reference.
 */
static unsigned int inode_read_block(ext2_fs_t * this, ext2_inodetable_t * inode, unsigned int no, unsigned int block, uint8_t * buf) {

	if (block >= inode->blocks) {
		memset(buf, 0x00, this->block-size);
		debug_print(CRITICAL, "Tried to read an invalid block. Asked for %d, but inode only has %d!", block, inode->blocks);
		return 0;
	}

	unsigned int real_block = get_block_number(this, inode, block);
	read_block(this, real_block, buf);

	return real_block;
}

/**
 * ext2->inode_write_block
 */
static unsigned int inode_write_block(ext2_fs_t * this, ext2_inodetable_t * inode, unsigned int inode_no, unsigned int block, uint8_t * buf) {
	if (block >= inode->blocks) {
		debug_print(WARNING, "Attempting to write beyond the existing allocated blocks for this inode.\n");
		debug_print(WARNING, "Inode %d, Block %d", inode_no, block);
	}

	while (block >= inode->blocks) {
		allocate_inode_block(this, inode, inode_no, inode->blocks);
		if (block != inode->blocks - 1) {
			unsigned int real_block = get_block_number(this, inode, inode->blocks - 1);
			uint8_t empty[this->block_size];
			memset(&empty, 0x00, this->block_size);
			write_block(this, real_block, &empty);
		}
	}

	unsigned int real_block = get_block_number(this, inode, block);
	debug_print(INFO, "Writing virtual block %d for inode %d maps to real block %d", block, inode_no, real_block);

	write_block(this, real_block, buf);
	return real_block;
}

#if 0
/**
 * ext2->create_entry
 *
 * @returns Error code or E_SUCCESS
 */
static int create_entry(fs_node_t * parent, char * name, uint16_t permission) {
	ext2_fs_t * this = (ext2_fs_t *)parent->device;

	debug_print(NOTICE, "Creating file in EXT2 fs: %s", name);
	debug_print(NOTICE, "Requested file permissions: %x", permission);
	uint16_t mode = permission | EXT2_S_IFREG; /* Set file mode to 'regular' */

	fs_node_t * tmp = finddir_ext2_dis

}

static int allocate_inode(ext2_fs_t * this, ext2_inodetable_t * parent, unsigned int no, char * name, uint16_t mode, uint32_t * inode_no, ext2_inodetable_t * inode) {
	if (((parent->mode & EXT2_S_IFDIR) == 0) || (name == NULL)) {
		debug_print(WARNING, "Attempted to allocate an inode in a parent that was not a directory.");
		return E_BADPARENT;
	}

	uint32_t node_no     = 0;
	uint32_t node_offset = 0;
	uint32_t group       = 0;
	uint8_t  bg_buffer[this->block_size];

	for (unsigned int i = 0; i < BGDS; ++i) {
		if (BGD[i].free_inodes_count > 0) {
			debug_print(NOTICE, "Group %d has %d free inodes.", i, BGD[i].free_inodes_count);
			read_block(this, BGD[i].inode_bitmap, (uint8_t *)&bg_buffer);
			while (BLOCKBIT(node_offset)) {
				node_offset++;
			}
			node_no = node_offset + this->inodes_per_group;
			group = i;
			break;
		}
	}
	if (!node_no) {
		debug_print(ERROR, "Ran out of inodes!");
		return E_NOSPACE;
	}

	BLOCKBYTE(node_offset) |= SETBIT(node_offset);

	write_block(this, BGD[group].inode_bitmap, (uint8_t *)bg_buffer);
	BGD[group].free_inodes_count--;
	write_block(this, EXT2_BGD_BLOCK, (uint8_t *)BGD);

	inode
}
#endif

#endif

int ext2_initialize(void) {

	return 0;
}

int ext2_finalize(void) {

	return 0;
}


MODULE_DEF(ext2, ext2_initialize, ext2_finalize);

