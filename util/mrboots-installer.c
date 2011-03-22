/*
 * Mr Boots Installer
 *
 * Installs Mr. Boots onto a generated disk image.
 * Compile me with your standard C library and for whatever
 * architecture you feel like running me on, though I much
 * prefer something simple and 32-bit.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
/* The EXT2 header is smart enough to know to grab us stdint.h rather than types.h... */
#include "../kernel/include/ext2.h"

#define ext2_get_block(block) ((uintptr_t)hdd_dump + (0x400 << sblock->log_block_size) * block)

char * hdd_dump = NULL;
ext2_superblock_t * sblock;
ext2_inodetable_t * itable;

ext2_inodetable_t *
ext2_get_inode(
		uint32_t inode
		) {
	return (ext2_inodetable_t *)((uintptr_t)itable + sblock->inode_size * (inode - 1));
}

ext2_inodetable_t *
ext2_finddir(
		ext2_inodetable_t * rnode,
		char * name
		) {

	void       * block;
	ext2_dir_t * direntry = NULL;
	block = (void *)ext2_get_block((rnode->block[0]));
	
	uint32_t dir_offset;
	dir_offset = 0;
	/*
	 * Look through the requested entries until we find what we're looking for
	 */
	while (dir_offset < rnode->size) {
		ext2_dir_t * d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		char * dname = malloc(sizeof(char) * (d_ent->name_len + 1));
		memcpy(dname, &d_ent->name, d_ent->name_len);
		dname[d_ent->name_len] = '\0';
		if (!strcmp(dname, name)) {
			free(dname);
			direntry = d_ent;
			break;
		}
		free(dname);
		dir_offset += d_ent->rec_len;
	}
	if (!direntry) {
		/*
		 * We could not find the requested entry in this directory.
		 */
		fprintf(stderr, "Failed to locate %s!\n", name);
		return NULL;
	} else {
		return ext2_get_inode(direntry->inode);
	}

}

/*
 * Retreive the node for the requested path
 */
ext2_inodetable_t *
iopen(
		ext2_inodetable_t * root,
		const char *filename
	 ) {
	size_t path_len = strlen(filename);
	if (path_len == 1) {
		return root;
	}
	char * path = (char *)malloc(sizeof(char) * (path_len + 1));
	memcpy(path, filename, path_len);
	char * path_offset = path;
	uint32_t path_depth = 0;
	while (path_offset < path + path_len) {
		if (*path_offset == '/') {
			*path_offset = '\0';
			path_depth++;
		}
		path_offset++;
	}
	path[path_len] = '\0';
	path_offset = path + 1;
	uint32_t depth;
	ext2_inodetable_t * node_ptr = root;
	for (depth = 0; depth < path_depth; ++depth) {
		node_ptr = ext2_finddir(node_ptr, path_offset);
		if (!node_ptr) {
			free((void *)path);
			return NULL;
		} else if (depth == path_depth - 1) {
			return node_ptr;
		}
		path_offset += strlen(path_offset) + 1;
	}
	free((void *)path);
	return NULL;
}

uint32_t
ext2_get_inode_block_num(
		ext2_inodetable_t * inode,
		uint32_t block
		) {
	if (block < 12) {
		return inode->block[block];
	} else if (block < 12 + (1024 << sblock->log_block_size) / sizeof(uint32_t)) {
		return *(uint32_t*)((uintptr_t)ext2_get_block(inode->block[12]) + (block - 12) * sizeof(uint32_t));
	}
	return 0;
}

int main(int argc, char ** argv) {
	if (argc < 3) {
		fprintf(stderr, "Expected two additional arguments: a ramdisk, and a file path to second stage to find in it.\n");
		return -1;
	}
	fprintf(stderr, "I will look for %s in %s and generate appropriate output.\n", argv[2], argv[1]);
	/* Open sesame! */
	FILE * hdd = fopen(argv[1], "r");
	fseek(hdd, 0, SEEK_END);
	/* Get size of file */
	uint32_t hdd_size = ftell(hdd);
	fprintf(stderr, "HDD image is %d bytes.\n", hdd_size);
	fseek(hdd, 0, SEEK_SET);
	/* Allocate us up some mems for the hard disk image */
	hdd_dump = malloc(sizeof(char) * hdd_size);
	/* Read 'er in. */
	fread(hdd_dump, hdd_size, 1, hdd);
	/* And lets make us some pointers. */
	sblock = (ext2_superblock_t *)((uintptr_t)hdd_dump + 0x400);
	fprintf(stderr, "Superblock magic is 0x%x\n", sblock->magic);
	assert(sblock->magic == EXT2_SUPER_MAGIC);
	if (sblock->inode_size == 0) {
		sblock->inode_size = 128;
	}
	fprintf(stdout,"INODE_SIZE = 0x%x\n", sblock->inode_size);
	fprintf(stdout,"BLOCK_SIZE = 0x%x\n", 0x400 << sblock->log_block_size);
	/* More pointers! */
	ext2_bgdescriptor_t * rblock = (ext2_bgdescriptor_t *)((uintptr_t)hdd_dump + 0x400 + 0x400);
	fprintf(stderr,"INODE_TABL = 0x%x\n", rblock->inode_table);
	/* Inode table */
	itable = (ext2_inodetable_t   *)((uintptr_t)hdd_dump + (0x400 << sblock->log_block_size) * rblock->inode_table);
	/* Root node */
	ext2_inodetable_t   * rnode  = (ext2_inodetable_t   *)((uintptr_t)itable + sblock->inode_size);
	fprintf(stderr, "Pretty sure everything is right so far...\n");

	ext2_inodetable_t   * fnode  = iopen(rnode, argv[2]);
	if (!fnode) {
		fprintf(stderr,"Failed to locate the requested file on the disk image.\n");
		return -1;
	}
	fprintf(stdout,"FILE_SIZE   = 0x%x\n", fnode->size);
	fprintf(stdout,"BLOCKS = { ");
	for (uint32_t i = 0; i < fnode->blocks; ++i) {
		uint32_t block = ext2_get_inode_block_num(fnode, i);
		fprintf(stdout, "%d", block);
		if (ext2_get_inode_block_num(fnode, i+1) == 0) {
			fprintf(stdout, " }\n");
			fprintf(stdout, "BLOCK_COUNT = 0x%x\n", i + 1);
			break;
		} else {
			fprintf(stdout, ",");
		};
	}
}
