/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <types.h>
#include <fs.h>
#include <ext2.h>
#include <logging.h>
#include <module.h>
#include <args.h>

#define EXT2_BGD_BLOCK 2

#define E_SUCCESS   0
#define E_BADBLOCK  1
#define E_NOSPACE   2
#define E_BADPARENT 3

/*
 * EXT2 filesystem object
 */
typedef struct {
	ext2_superblock_t       * superblock;          /* Device superblock, contains important information */
	ext2_bgdescriptor_t     * block_groups;        /* Block Group Descriptor / Block groups */
	fs_node_t               * root_node;           /* Root FS node (attached to mountpoint) */

	fs_node_t               * block_device;        /* Block device node XXX unused */

	unsigned int              block_size;          /* Size of one block */
	unsigned int              pointers_per_block;  /* Number of pointers that fit in a block */
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
#define BGDS (this->block_group_count)
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

static uint32_t node_from_file(ext2_fs_t * this, ext2_inodetable_t *inode, ext2_dir_t *direntry,  fs_node_t *fnode);
static uint32_t ext2_root(ext2_fs_t * this, ext2_inodetable_t *inode, fs_node_t *fnode);
static ext2_inodetable_t * read_inode(ext2_fs_t * this, uint32_t inode);

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
	write_fs(this->block_device, (DC[ent_no].block_no) * this->block_size, this->block_size, (uint8_t *)(DC[ent_no].block));
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
		return E_BADBLOCK;
	}

	/* This operation requires the filesystem lock to be obtained */
	spin_lock(&this->lock);

	/* We can make reads without a cache in place. */
	if (!DC) {
		/* In such cases, we read directly from the block device */
		read_fs(this->block_device, block_no * this->block_size, this->block_size, (uint8_t *)buf);
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
	read_fs(this->block_device, block_no * this->block_size, this->block_size, (uint8_t *)DC[oldest].block);

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
 * @returns Error code or E_SUCCESSS
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
			return E_SUCCESS;
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
	DC[oldest].last_use = get_cache_time(this);
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
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS], (uint8_t *)&tmp);

		((uint32_t *)&tmp)[iblock - EXT2_DIRECT_BLOCKS] = rblock;
		write_block(this, inode->block[EXT2_DIRECT_BLOCKS], (uint8_t *)&tmp);

		return E_SUCCESS;
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b / p;
		d = b - c * p;

		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 1], (uint8_t *)&tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[c];
		read_block(this, nblock, (uint8_t *)&tmp);

		((uint32_t  *)&tmp)[d] = rblock;
		write_block(this, nblock, (uint8_t *)&tmp);

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
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 2], (uint8_t *)&tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[d];
		read_block(this, nblock, (uint8_t *)&tmp);

		nblock = ((uint32_t *)&tmp)[f];
		read_block(this, nblock, (uint8_t *)&tmp);

		((uint32_t *)&tmp)[g] = nblock;
		write_block(this, nblock, (uint8_t *)&tmp);

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
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS], (uint8_t *)&tmp);

		return ((uint32_t *)&tmp)[iblock - EXT2_DIRECT_BLOCKS];
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b / p;
		d = b - c * p;

		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 1], (uint8_t *)&tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[c];
		read_block(this, nblock, (uint8_t *)&tmp);

		return ((uint32_t  *)&tmp)[d];
	} else if (iblock < EXT2_DIRECT_BLOCKS + p + p * p + p) {
		a = iblock - EXT2_DIRECT_BLOCKS;
		b = a - p;
		c = b - p * p;
		d = c / (p * p);
		e = c - d * p * p;
		f = e / p;
		g = e - f * p;

		uint8_t tmp[this->block_size];
		read_block(this, inode->block[EXT2_DIRECT_BLOCKS + 2], (uint8_t *)&tmp);

		uint32_t nblock = ((uint32_t *)&tmp)[d];
		read_block(this, nblock, (uint8_t *)&tmp);

		nblock = ((uint32_t *)&tmp)[f];
		read_block(this, nblock, (uint8_t *)&tmp);

		return ((uint32_t *)&tmp)[g];
	}

	debug_print(CRITICAL, "EXT2 driver tried to read to a block number that was too high (%d)", iblock);

	return 0;
}

static int write_inode(ext2_fs_t * this, ext2_inodetable_t *inode, uint32_t index) {
	uint32_t group = index / this->inodes_per_group;
	if (group > BGDS) {
		return E_BADBLOCK;
	}
	
	uint32_t inode_table_block = BGD[group].inode_table;
	index -= group * this->inodes_per_group;
	uint32_t block_offset = ((index - 1) * SB->inode_size) / this->block_size;
	uint32_t offset_in_block = (index - 1) - block_offset * (this->block_size / SB->inode_size);

	ext2_inodetable_t *inodet = malloc(this->block_size);
	/* Read the current table block */
	read_block(this, inode_table_block + block_offset, (uint8_t *)inodet);
	memcpy((uint8_t *)((uint32_t)inodet + offset_in_block * SB->inode_size), inode, SB->inode_size);
	write_block(this, inode_table_block + block_offset, (uint8_t *)inodet);
	free(inodet);

	return E_SUCCESS;
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
			read_block(this, BGD[i].block_bitmap, (uint8_t *)&bg_buffer);
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
	write_block(this, BGD[group].block_bitmap, (uint8_t *)&bg_buffer);

	set_block_number(this, inode, block, block_no);

	BGD[group].free_blocks_count--;
	write_block(this, EXT2_BGD_BLOCK, (uint8_t *)BGD);

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
		memset(buf, 0x00, this->block_size);
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
		debug_print(WARNING, "Attempting to write beyond the existing allocated blocks for this inode.");
		debug_print(WARNING, "Inode %d, Block %d", inode_no, block);
	}

	while (block >= inode->blocks) {
		allocate_inode_block(this, inode, inode_no, inode->blocks);
		if (block != inode->blocks - 1) {
			unsigned int real_block = get_block_number(this, inode, inode->blocks - 1);
			uint8_t empty[this->block_size];
			memset(&empty, 0x00, this->block_size);
			write_block(this, real_block, (uint8_t *)&empty);
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

/**
 * direntry_ext2
 */
static ext2_dir_t * direntry_ext2(ext2_fs_t * this, ext2_inodetable_t * inode, uint32_t no, uint32_t index) {
	uint8_t *block = malloc(this->block_size);
	uint8_t block_nr = 0;
	inode_read_block(this, inode, no, block_nr, block);
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

		if (dir_offset >= this->block_size) {
			block_nr++;
			dir_offset -= this->block_size;
			inode_read_block(this, inode, no, block_nr, block);
		}
	}
	
	free(block);
	return NULL;
}

/**
 * finddir_ext2
 */
static fs_node_t * finddir_ext2(fs_node_t *node, char *name) {

	ext2_fs_t * this = (ext2_fs_t *)node->device;

	ext2_inodetable_t *inode = read_inode(this,node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	uint8_t block[this->block_size];
	ext2_dir_t *direntry = NULL;
	uint8_t block_nr = 0;
	inode_read_block(this, inode, node->inode, block_nr, block);
	uint32_t dir_offset = 0;
	uint32_t total_offset = 0;

	while (total_offset < inode->size) {
		if (dir_offset >= this->block_size) {
			block_nr++;
			dir_offset -= this->block_size;
			inode_read_block(this, inode, node->inode, block_nr, block);
		}
		ext2_dir_t *d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		
		if (strlen(name) != d_ent->name_len) {
			dir_offset += d_ent->rec_len;
			total_offset += d_ent->rec_len;

			continue;
		}

		char *dname = malloc(sizeof(char) * (d_ent->name_len + 1));
		memcpy(dname, &(d_ent->name), d_ent->name_len);
		dname[d_ent->name_len] = '\0';
		if (!strcmp(dname, name)) {
			free(dname);
			direntry = malloc(d_ent->rec_len);
			memcpy(direntry, d_ent, d_ent->rec_len);
			break;
		}
		free(dname);

		dir_offset += d_ent->rec_len;
		total_offset += d_ent->rec_len;
	}
	free(inode);
	if (!direntry) {
		return NULL;
	}
	fs_node_t *outnode = malloc(sizeof(fs_node_t));
	memset(outnode, 0, sizeof(fs_node_t));

	inode = read_inode(this, direntry->inode);

	if (!node_from_file(this, inode, direntry, outnode)) {
		debug_print(CRITICAL, "Oh dear. Couldn't allocate the outnode?");
	}

	free(direntry);
	free(inode);
	return outnode;
}


/**
 * read_inode
 */
static ext2_inodetable_t * read_inode(ext2_fs_t * this, uint32_t inode) {
	uint32_t group = inode / this->inodes_per_group;
	if (group > BGDS) {
		return NULL;
	}
	uint32_t inode_table_block = BGD[group].inode_table;
	inode -= group * this->inodes_per_group;	// adjust index within group
	uint32_t block_offset		= ((inode - 1) * SB->inode_size) / this->block_size;
	uint32_t offset_in_block    = (inode - 1) - block_offset * (this->block_size / SB->inode_size);

	uint8_t buf[this->block_size];
	ext2_inodetable_t *inodet   = malloc(SB->inode_size);

	read_block(this, inode_table_block + block_offset, buf);
	ext2_inodetable_t *inodes = (ext2_inodetable_t *)buf;

	memcpy(inodet, (uint8_t *)((uint32_t)inodes + offset_in_block * SB->inode_size), SB->inode_size);

	return inodet;
}

static uint32_t read_ext2(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	ext2_fs_t * this = (ext2_fs_t *)node->device;
	ext2_inodetable_t * inode = read_inode(this, node->inode);
	uint32_t end;
	if (offset + size > inode->size) {
		end = inode->size;
	} else {
		end = offset + size;
	}
	uint32_t start_block  = offset / this->block_size;
	uint32_t end_block    = end / this->block_size;
	uint32_t end_size     = end - end_block * this->block_size;
	uint32_t size_to_read = end - offset;
	if (end_size == 0) {
		end_block--;
	}
	if (start_block == end_block) {
		uint8_t buf[this->block_size];
		inode_read_block(this, inode, node->inode, start_block, buf);
		memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % this->block_size)), size_to_read);
		free(inode);
		return size_to_read;
	} else {
		uint32_t block_offset;
		uint32_t blocks_read = 0;
		uint8_t buf[this->block_size];
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				inode_read_block(this, inode, node->inode, block_offset, buf);
				memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % this->block_size)), this->block_size - (offset % this->block_size));
			} else {
				inode_read_block(this, inode, node->inode, block_offset, buf);
				memcpy(buffer + this->block_size * blocks_read - (offset % this->block_size), buf, this->block_size);
			}
		}
		inode_read_block(this, inode, node->inode, end_block, buf);
		memcpy(buffer + this->block_size * blocks_read - (offset % this->block_size), buf, end_size);
	}
	free(inode);
	return size_to_read;
}


static void open_ext2(fs_node_t *node, unsigned int flags) {
	/* Nothing to do here */
}

static void close_ext2(fs_node_t *node) {
	/* Nothing to do here */
}


/**
 * readdir_ext2
 */
static struct dirent * readdir_ext2(fs_node_t *node, uint32_t index) {

	ext2_fs_t * this = (ext2_fs_t *)node->device;

	ext2_inodetable_t *inode = read_inode(this, node->inode);
	assert(inode->mode & EXT2_S_IFDIR);
	ext2_dir_t *direntry = direntry_ext2(this, inode, node->inode, index);
	if (!direntry) {
		free(inode);
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

static uint32_t node_from_file(ext2_fs_t * this, ext2_inodetable_t *inode, ext2_dir_t *direntry,  fs_node_t *fnode) {
	if (!fnode) {
		/* You didn't give me a node to write into, go **** yourself */
		return 0;
	}
	/* Information from the direntry */
	fnode->device = (void *)this;
	fnode->inode = direntry->inode;
	memcpy(&fnode->name, &direntry->name, direntry->name_len);
	fnode->name[direntry->name_len] = '\0';
	/* Information from the inode */
	fnode->uid = inode->uid;
	fnode->gid = inode->gid;
	fnode->length = inode->size;
	fnode->mask = inode->mode & 0xFFF;
	fnode->nlink = inode->links_count;
	/* File Flags */
	fnode->flags = 0;
	if ((inode->mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
		fnode->flags |= FS_FILE;
		fnode->create = NULL;
		fnode->mkdir = NULL;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
		fnode->create = NULL; // ext2_create;
		fnode->mkdir = NULL; // ext2_mkdir;
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

	fnode->atime   = inode->atime;
	fnode->mtime   = inode->mtime;
	fnode->ctime   = inode->ctime;
	debug_print(INFO, "file a/m/c times are %d/%d/%d", fnode->atime, fnode->mtime, fnode->ctime);

	fnode->read    = read_ext2;
	fnode->write   = NULL; //write_ext2;
	fnode->open    = open_ext2;
	fnode->close   = close_ext2;
	fnode->readdir = readdir_ext2;
	fnode->finddir = finddir_ext2;
	fnode->ioctl   = NULL;
	return 1;
}

static uint32_t ext2_root(ext2_fs_t * this, ext2_inodetable_t *inode, fs_node_t *fnode) {
	if (!fnode) {
		return 0;
	}
	/* Information for root dir */
	fnode->device = (void *)this;
	fnode->inode = 2;
	fnode->name[0] = '/';
	fnode->name[1] = '\0';
	/* Information from the inode */
	fnode->uid = inode->uid;
	fnode->gid = inode->gid;
	fnode->length = inode->size;
	fnode->mask = inode->mode & 0xFFF;
	fnode->nlink = inode->links_count;
	/* File Flags */
	fnode->flags = 0;
	if ((inode->mode & EXT2_S_IFREG) == EXT2_S_IFREG) {
		debug_print(CRITICAL, "Root appears to be a regular file.");
		debug_print(CRITICAL, "This is probably very, very wrong.");
		return 0;
	}
	if ((inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
		fnode->flags |= FS_DIRECTORY;
		fnode->create = NULL; //ext2_create;
		fnode->mkdir = NULL; //ext2_mkdir;
	} else {
		debug_print(CRITICAL, "Root doesn't appear to be a directory.");
		debug_print(CRITICAL, "This is probably very, very wrong.");
		return 0;
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

	fnode->atime   = inode->atime;
	fnode->mtime   = inode->mtime;
	fnode->ctime   = inode->ctime;

	fnode->read    = read_ext2;
	fnode->write   = NULL;
	fnode->open    = open_ext2;
	fnode->close   = close_ext2;
	fnode->readdir = readdir_ext2;
	fnode->finddir = finddir_ext2;
	fnode->ioctl   = NULL;
	return 1;
}

static fs_node_t * mount_ext2(fs_node_t * block_device) {

	debug_print(NOTICE, "Mounting ext2 file system...");
	ext2_fs_t * this = malloc(sizeof(ext2_fs_t));

	memset(this, 0x00, sizeof(ext2_fs_t));

	this->block_device = block_device;
	this->block_size = 1024;

	SB = malloc(this->block_size);

	debug_print(INFO, "Reading superblock...");
	read_block(this, 1, (uint8_t *)SB);
	if (SB->magic != EXT2_SUPER_MAGIC) {
		debug_print(ERROR, "... not an EXT2 filesystem? (magic didn't match, got 0x%x)", SB->magic);
		return NULL;
	}
	if (SB->inode_size == 0) {
		SB->inode_size = 128;
	}
	this->block_size = 1024 << SB->log_block_size;
	this->cache_entries = 10240;
	if (this->block_size > 2048) {
		this->cache_entries /= 4;
	}
	debug_print(INFO, "bs=%d, cache entries=%d", this->block_size, this->cache_entries);
	this->pointers_per_block = this->block_size / 4;
	debug_print(INFO, "Log block size = %d -> %d", SB->log_block_size, this->block_size);
	BGDS = SB->blocks_count / SB->blocks_per_group;
	if (SB->blocks_per_group * BGDS < SB->blocks_count) {
		BGDS += 1;
	}
	this->inodes_per_group = SB->inodes_count / BGDS;

	debug_print(INFO, "Allocating cache...");
	DC = malloc(sizeof(ext2_disk_cache_entry_t) * this->cache_entries);
	for (uint32_t i = 0; i < this->cache_entries; ++i) {
		DC[i].block = malloc(this->block_size);
		if (i % 128 == 0) {
			debug_print(INFO, "Allocated cache block #%d", i+1);
		}
	}
	debug_print(INFO, "Allocated cache.");

	// load the block group descriptors
	int bgd_block_span = sizeof(ext2_bgdescriptor_t) * BGDS / this->block_size + 1;
	BGD = malloc(this->block_size * bgd_block_span);

	debug_print(INFO, "bgd_block_span = %d", bgd_block_span);

	int bgd_offset = 2;

	if (this->block_size > 1024) {
		bgd_offset = 1;
	}

	for (int i = 0; i < bgd_block_span; ++i) {
		read_block(this, bgd_offset + i, (uint8_t *)((uint32_t)BGD + this->block_size * i));
	}

#ifdef DEBUG_BLOCK_DESCRIPTORS
	char * bg_buffer = malloc(this->block_size * sizeof(char));
	for (uint32_t i = 0; i < BGDS; ++i) {
		debug_print(INFO, "Block Group Descriptor #%d @ %d", i, bgd_offset + i * SB->blocks_per_group);
		debug_print(INFO, "\tBlock Bitmap @ %d", BGD[i].block_bitmap); { 
			debug_print(INFO, "\t\tExamining block bitmap at %d", BGD[i].block_bitmap);
			read_block(this, BGD[i].block_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			debug_print(INFO, "\t\tFirst free block in group is %d", j + BGD[i].block_bitmap - 2);
		}
		debug_print(INFO, "\tInode Bitmap @ %d", BGD[i].inode_bitmap); {
			debug_print(INFO, "\t\tExamining inode bitmap at %d", BGD[i].inode_bitmap);
			read_block(this, BGD[i].inode_bitmap, (uint8_t *)bg_buffer);
			uint32_t j = 0;
			while (BLOCKBIT(j)) {
				++j;
			}
			debug_print(INFO, "\t\tFirst free inode in group is %d", j + this->inodes_per_group * i + 1);
		}
		debug_print(INFO, "\tInode Table  @ %d", BGD[i].inode_table);
		debug_print(INFO, "\tFree Blocks =  %d", BGD[i].free_blocks_count);
		debug_print(INFO, "\tFree Inodes =  %d", BGD[i].free_inodes_count);
	}
	free(bg_buffer);
#endif

	ext2_inodetable_t *root_inode = read_inode(this, 2);
	RN = (fs_node_t *)malloc(sizeof(fs_node_t));
	if (!ext2_root(this, root_inode, RN)) {
		debug_print(NOTICE, "Oh dear...");
	}
	debug_print(NOTICE, "Root file system is ready.");
	debug_print(NOTICE, "Mounted EXT2 disk, root VFS node is at 0x%x", RN);
	return RN;
}

fs_node_t * ext2_fs_mount(char * device, char * mount_path) {
	fs_node_t * dev = kopen(device, 0);
	if (!dev) {
		debug_print(ERROR, "failed to open %s", device);
		return NULL;
	}
	fs_node_t * fs = mount_ext2(dev);
	return fs;
}

int ext2_initialize(void) {

	vfs_register("ext2", ext2_fs_mount);

	return 0;
}

int ext2_finalize(void) {

	return 0;
}

MODULE_DEF(ext2, ext2_initialize, ext2_finalize);

