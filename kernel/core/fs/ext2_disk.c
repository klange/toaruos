#include <system.h>
#include <ext2.h>
#include <fs.h>

#define BLOCKSIZE 1024
#define SECTORSIZE 512


ext2_superblock_t * ext2_disk_superblock = NULL;

#define SB ext2_disk_superblock

static uint32_t btos(uint32_t block) {
	return block * (BLOCKSIZE / SECTORSIZE); 
}

void ext2_disk_read_superblock() {
	uint8_t * buf = malloc(BLOCKSIZE);
	ide_read_sector(0x1F0, 0, btos(1) + 0, buf);
	ide_read_sector(0x1F0, 0, btos(1) + 1, (uint8_t *)((uint32_t)buf + SECTORSIZE));
	SB = (ext2_superblock_t *)buf;
	kprintf("Volume '%s'\n", SB->volume_name);
	kprintf("%d inodes\n", SB->inodes_count);
	kprintf("%d blocks\n", SB->blocks_count);
	kprintf("%d free blocks\n", SB->free_blocks_count);
	kprintf("0x%x last mount time\n", SB->mtime);
	kprintf("0x%x last write time\n", SB->wtime);
	kprintf("Mounted %d times.\n", SB->mnt_count);
	kprintf("0x%x\n", SB->magic);
}

void ext2_disk_forget_superblock() {
	free(SB);
}
