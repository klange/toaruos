/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef EXT2_H
#define EXT2_H

#ifdef _KERNEL_
#	include <types.h>
#else
#	ifdef BOOTLOADER
#		include <types.h>
#	else
#		include <stdint.h>
#	endif
#endif

#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_DIRECT_BLOCKS 12

/* Super block struct. */
struct ext2_superblock {
	uint32_t inodes_count;
	uint32_t blocks_count;
	uint32_t r_blocks_count;
	uint32_t free_blocks_count;
	uint32_t free_inodes_count;
	uint32_t first_data_block;
	uint32_t log_block_size;
	uint32_t log_frag_size;
	uint32_t blocks_per_group;
	uint32_t frags_per_group;
	uint32_t inodes_per_group;
	uint32_t mtime;
	uint32_t wtime;

	uint16_t mnt_count;
	uint16_t max_mnt_count;
	uint16_t magic;
	uint16_t state;
	uint16_t errors;
	uint16_t minor_rev_level;

	uint32_t lastcheck;
	uint32_t checkinterval;
	uint32_t creator_os;
	uint32_t rev_level;

	uint16_t def_resuid;
	uint16_t def_resgid;

	/* EXT2_DYNAMIC_REV */
	uint32_t first_ino;
	uint16_t inode_size;
	uint16_t block_group_nr;
	uint32_t feature_compat;
	uint32_t feature_incompat;
	uint32_t feature_ro_compat;

	uint8_t uuid[16];
	uint8_t volume_name[16];

	uint8_t last_mounted[64];

	uint32_t algo_bitmap;

	/* Performance Hints */
	uint8_t prealloc_blocks;
	uint8_t prealloc_dir_blocks;
	uint16_t _padding;

	/* Journaling Support */
	uint8_t journal_uuid[16];
	uint32_t journal_inum;
	uint32_t jounral_dev;
	uint32_t last_orphan;

	/* Directory Indexing Support */
	uint32_t hash_seed[4];
	uint8_t def_hash_version;
	uint16_t _padding_a;
	uint8_t _padding_b;

	/* Other Options */
	uint32_t default_mount_options;
	uint32_t first_meta_bg;
	uint8_t _unused[760];

} __attribute__ ((packed));

typedef struct ext2_superblock ext2_superblock_t;

/* Block group descriptor. */
struct ext2_bgdescriptor {
	uint32_t block_bitmap;
	uint32_t inode_bitmap;		// block no. of inode bitmap
	uint32_t inode_table;
	uint16_t free_blocks_count;
	uint16_t free_inodes_count;
	uint16_t used_dirs_count;
	uint16_t pad;
	uint8_t reserved[12];
} __attribute__ ((packed));

typedef struct ext2_bgdescriptor ext2_bgdescriptor_t;

/* File Types */
#define EXT2_S_IFSOCK	0xC000
#define EXT2_S_IFLNK	0xA000
#define EXT2_S_IFREG	0x8000
#define EXT2_S_IFBLK	0x6000
#define EXT2_S_IFDIR	0x4000
#define EXT2_S_IFCHR	0x2000
#define EXT2_S_IFIFO	0x1000

/* setuid, etc. */
#define EXT2_S_ISUID	0x0800
#define EXT2_S_ISGID	0x0400
#define EXT2_S_ISVTX	0x0200

/* rights */
#define EXT2_S_IRUSR	0x0100
#define EXT2_S_IWUSR	0x0080
#define EXT2_S_IXUSR	0x0040
#define EXT2_S_IRGRP	0x0020
#define EXT2_S_IWGRP	0x0010
#define EXT2_S_IXGRP	0x0008
#define EXT2_S_IROTH	0x0004
#define EXT2_S_IWOTH	0x0002
#define EXT2_S_IXOTH	0x0001

/* This is not actually the inode table.
 * It represents an inode in an inode table on disk. */
struct ext2_inodetable {
	uint16_t mode;
	uint16_t uid;
	uint32_t size;			// file length in byte.
	uint32_t atime;
	uint32_t ctime;
	uint32_t mtime;
	uint32_t dtime;
	uint16_t gid;
	uint16_t links_count;
	uint32_t blocks;
	uint32_t flags;
	uint32_t osd1;
	uint32_t block[15];
	uint32_t generation;
	uint32_t file_acl;
	uint32_t dir_acl;
	uint32_t faddr;
	uint8_t osd2[12];
} __attribute__ ((packed));

typedef struct ext2_inodetable ext2_inodetable_t;

/* Represents directory entry on disk. */
struct ext2_dir {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
	char name[];		/* Actually a set of characters, at most 255 bytes */
} __attribute__ ((packed));

typedef struct ext2_dir ext2_dir_t;

typedef struct {
	uint32_t block_no;
	uint32_t last_use;
	uint8_t  dirty;
	uint8_t *block;
} ext2_disk_cache_entry_t;

typedef int (*ext2_block_io_t) (void *, uint32_t, uint8_t *);

#endif

