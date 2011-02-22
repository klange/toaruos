/*
 * Mr. Boots Stage 2 C Main
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#define BOOTLOADER
#include <ext2.h>

#define EXT2_SUPER_OFFSET 0x1000
#define EXT2_START (EXT2_SUPER_OFFSET - 0x400)

#define ext2_get_block(block) (EXT2_START + (0x400 << sblock->log_block_size) * block)

#define PRINT(s) __asm__ ("movw %0, %%si\ncall _print" : : "l"((short)(int)s))

void read(unsigned char count, unsigned char sector, short segment, short offset)
{
	__asm__ __volatile__ (
			"movw %0, %%es\n"
			"movw %1, %%bx\n"
			"movb %2, %%al\n"
			"movb $0, %%ch\n"
			"movb %3, %%cl\n"
			"movb $0, %%dh\n"
			"movb $0x80, %%dl\n"
			"movb $0x02, %%ah\n"
			"int $0x13" : :
			"l" (segment),
			"l" (offset),
			"m" (count),
			"m" (sector));
}

/*
 * Main entry point
 */
void main()
{
	PRINT("M");
	/*
	 * Load up the superblock
	 */
	read(16,3,0,EXT2_SUPER_OFFSET);
	ext2_superblock_t   * sblock = (ext2_superblock_t   *)(EXT2_START + 0x400);
	if (sblock->magic != EXT2_SUPER_MAGIC) {
		goto failure;
	}
	if (sblock->inode_size == 0) {
		sblock->inode_size = 128;
	}
	ext2_bgdescriptor_t * rblock = (ext2_bgdescriptor_t *)(EXT2_START + 0x400 + 0x400);
	ext2_inodetable_t   * itable = (ext2_inodetable_t   *)(EXT2_START + (0x400 << sblock->log_block_size) * rblock->inode_table);
	ext2_inodetable_t   * rnode  = (ext2_inodetable_t   *)((unsigned short)(unsigned int)itable + (unsigned short)(unsigned int)sblock->inode_size);
	PRINT("r");

	/*
	 * Grab the first inode's data...
	 */
	read(2,9 + (ext2_get_block((rnode->block[0]))) / 0x200,0,EXT2_SUPER_OFFSET + 0xc00);
	void       * block;
	ext2_dir_t * direntry = NULL;
	block = (void *)ext2_get_block((rnode->block[0]));

	PRINT(". ");

	uint32_t     dir_offset = 0;
	dir_offset = 0;
	while (dir_offset < rnode->size) {
		ext2_dir_t * d_ent = (ext2_dir_t *)((unsigned short)(unsigned int)block + dir_offset);
		if (((char *)&d_ent->name)[0] == 'k'){
			direntry = d_ent;
			goto success;
		}
		dir_offset += d_ent->rec_len;
	}
	goto failure;
success:
	PRINT("B");


	PRINT("o");
	PRINT("o");
	PRINT("t");
	PRINT("s");

failure:
	PRINT("\023");
	/* And that's it for now... */
	__asm__ __volatile__ ("hlt");
	while (1) {};
}
