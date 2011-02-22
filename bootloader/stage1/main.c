/*
 * Mr. Boots Stage 2
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#include <ext2.h>

#define EXT2_SUPER_OFFSET 0x6000
#define EXT2_START 0x5C00

#define ext2_get_block(block) (void *)(EXT2_START + (0x400 << sblock->log_block_size * block))

/*
 * Read some sectors from the first hard drive
 */
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
	/*
	 * Read the EXT2 Super Block
	 */
	read(2,3,0,EXT2_SUPER_OFFSET);
	ext2_superblock_t * sblock = (ext2_superblock_t *)EXT2_SUPER_OFFSET;
	ext2_bgdescriptor_t * rblock = (ext2_bgdescriptor_t *)(EXT2_SUPER_OFFSET + 0x400);
	ext2_inodetable_t * itable = (ext2_inodetable_t *)(EXT2_START + (0x400 << sblock->log_block_size) * rblock->inode_table);
	ext2_inodetable_t * rnode = (ext2_inodetable_t *)((uintptr_t)itable + sblock->inode_size);
	void *       block;
	ext2_dir_t * direntry = NULL;
	block = (void *)ext2_get_block((rnode->block[0]));
	uint32_t dir_offset = 0;
	while (dir_offset < rnode->size) {
		ext2_dir_t * d_ent = (ext2_dir_t *)((uintptr_t)block + dir_offset);
		if (((char *)&d_ent->name)[0] == 's') {
			direntry = d_ent;
			break;
		}
		dir_offset += d_ent->rec_len;
	}
	*((unsigned int *)0x5000) = direntry->inode;
	read(1,2,0,0x7e00);
	/* Stage 2 */
	__asm__ __volatile__ ("jmp $0x00, $0x7e00");
}
