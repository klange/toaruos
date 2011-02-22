/*
 * Mr. Boots Stage 1
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#include <ext2.h>
#define EXT2_SUPER_OFFSET 0x6000
#define EXT2_START 0x5C00

/*
 * Main entry point
 */
void main()
{
	ext2_superblock_t * sblock = (ext2_superblock_t *)EXT2_SUPER_OFFSET;
	ext2_bgdescriptor_t * rblock = (ext2_bgdescriptor_t *)(EXT2_SUPER_OFFSET + 0x400);
	ext2_inodetable_t * itable = (ext2_inodetable_t *)(EXT2_START + (0x400 << sblock->log_block_size) * rblock->inode_table);
	ext2_inodetable_t * rnode = (ext2_inodetable_t *)((uintptr_t)itable + sblock->inode_size * (*(uint32_t *)(0x5000)));

}
