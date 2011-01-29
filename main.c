/*
 * Copyright (c) 2011 Kevin Lange.  All rights reserved.
 *
 * Developed by: ToAruOS Kernel Development Team
 *               http://github.com/klange/osdev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the ToAruOS Kernel Development Team, Kevin Lange,
 *      nor the names of its contributors may be used to endorse
 *      or promote products derived from this Software without specific prior
 *      written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 */

#include <system.h>
#include <multiboot.h>
#include <ext2.h>

/*
 * kernel entry point
 *
 * This is the C entry point for the kernel.
 * It is called by the assembly loader and is passed
 * multiboot information, if available, from the bootloader.
 *
 * The kernel boot process does the following:
 * - Align the dumb allocator's heap pointer
 * - Initialize the x86 descriptor tables (global, interrupts)
 * - Initialize the interrupt handlers (ISRS, IRQ)
 * - Load up the VGA driver.
 * - Initialize the hardware drivers (PIC, keyboard)
 * - Set up paging
 * - Initialize the kernel heap (klmalloc)
 * [Further booting]
 *
 * After booting, the kernel will display its version and dump the
 * multiboot data from the bootloader. It will then proceed to print
 * out the contents of the initial ramdisk image.
 */
int
main(struct multiboot *mboot_ptr) {

	/* Realing memory to the end of the multiboot modules */
	if (mboot_ptr->mods_count > 0) {
		uint32_t module_start = *((uint32_t*)mboot_ptr->mods_addr);
		uint32_t module_end   = *(uint32_t*)(mboot_ptr->mods_addr+4);
		kmalloc_startat(module_end);
	}
#if 0
	mboot_ptr = copy_multiboot(mboot_ptr);
#endif

	/* Initialize core modules */
	gdt_install();	/* Global descriptor table */
	idt_install();	/* IDT */
	isrs_install();	/* Interrupt service requests */
	irq_install();	/* Hardware interrupt requests */
	init_video();	/* VGA driver */

	/* Hardware drivers */
	timer_install();
	keyboard_install();

	/* Memory management */
	paging_install(mboot_ptr->mem_upper);
	heap_install();

	/* Kernel Version */
	settextcolor(12,0);
	kprintf("[%s %s]\n", KERNEL_UNAME, KERNEL_VERSION_STRING);

	/* Print multiboot information */
	dump_multiboot(mboot_ptr);

	uint32_t module_start = *((uint32_t*)mboot_ptr->mods_addr);
	uint32_t module_end   = *(uint32_t*)(mboot_ptr->mods_addr+4);

	initrd_mount(module_start, module_end);
	kprintf("Opening /etc/kernel/hello.txt... ");
	fs_node_t * test_file = kopen("/etc/kernel/hello.txt", NULL);
	if (!test_file) {
		kprintf("Couldn't find hello.txt\n");
	}
	kprintf("Found at inode %d\n", test_file->name, test_file->inode);
	char buffer[256];
	uint32_t bytes_read;
	bytes_read = read_fs(test_file, 0, 255, &buffer);
	kprintf("Read %d bytes from file:\n", bytes_read);
	kprintf("%s\n", buffer);
	kprintf("| end file\n");
	close_fs(test_file);

#if 0
	ext2_superblock_t * superblock = (ext2_superblock_t *)(module_start + 1024);
	kprintf("Magic is 0x%x\n", (int)superblock->magic);
	assert(superblock->magic == EXT2_SUPER_MAGIC);
	
	kprintf("Partition has %d inodes and %d blocks.\n", superblock->inodes_count, superblock->blocks_count);
	kprintf("%d blocks reserved for root\n", superblock->r_blocks_count);
	kprintf("%d blocks free\n", superblock->free_blocks_count);
	kprintf("%d free inodes\n", superblock->free_inodes_count);
	kprintf("Blocks contain %d bytes\n", 1024 << superblock->log_block_size);
	kprintf("Fragments contain %d bytes\n", 1024 << superblock->log_frag_size);
	kprintf("I am at block id: %d (should be 1 if this is a 1KB block)\n", superblock->first_data_block);
	kprintf("There are %d blocks in a group\n", superblock->blocks_per_group);
	kprintf("There are %d fragments in a group\n", superblock->frags_per_group);
	kprintf("There are %d inodes in a group\n", superblock->inodes_per_group);
	kprintf("Last mount: 0x%x\n", superblock->mtime);
	kprintf("Last write: 0x%x\n", superblock->wtime);
	kprintf("Mounts since verification: %d\n", superblock->mnt_count);
	kprintf("Must be verified in %d mounts\n", superblock->max_mnt_count - superblock->mnt_count);
	kprintf("Inodes are %d bytes\n", (int)superblock->inode_size);

	ext2_bgdescriptor_t * blockgroups = (ext2_bgdescriptor_t *)(module_start + 1024 + 1024);
	kprintf("First block group has %d free blocks, %d free inodes, %d used dirs\n",
			blockgroups->free_blocks_count,
			blockgroups->free_inodes_count,
			blockgroups->used_dirs_count);
	
	ext2_inodetable_t * inodetable = (ext2_inodetable_t *)(module_start + (1024 << superblock->log_block_size) * blockgroups->inode_table);
	uint32_t i;
	for (i = 0; i < superblock->inodes_per_group; ++i) {
		ext2_inodetable_t * inode = (ext2_inodetable_t *)((int)inodetable + (int)superblock->inode_size * i);
		if (inode->block[0] == 0)
			continue;
		kprintf("Inode %d starts at block %d,%d and is %d bytes (%d blocks). ", i, inode->block[0], inode->block[1], inode->size, inode->blocks);
		if (inode->mode & EXT2_S_IFDIR) {
			kprintf("is a directory\n");
			kprintf("File listing:\n");
			uint32_t dir_offset;
			dir_offset = 0;
			while (dir_offset < inode->size) {
				ext2_dir_t * d_ent = (ext2_dir_t *)(module_start + (1024 << superblock->log_block_size) * inode->block[0] + dir_offset);
				unsigned char * name = malloc(sizeof(unsigned char) * (d_ent->name_len + 1));
				memcpy(name, &d_ent->name, d_ent->name_len);
				name[d_ent->name_len] = '\0';
				kprintf("[%d] %s [%d]\n", dir_offset, name, d_ent->inode);
				if (name[0] == 'h' &&
					name[1] == 'e' &&
					name[2] == 'l' &&
					name[3] == 'l' &&
					name[4] == 'o') {
					kprintf("Found a file to read.\n");
					ext2_inodetable_t * inode_f = (ext2_inodetable_t *)((int)inodetable + (int)superblock->inode_size * (d_ent->inode -1));
					kprintf("Going to print %d bytes from block %d\n", inode_f->size, inode_f->block[0]);
					unsigned char * file_pointer = (unsigned char *)(module_start + (1024 << superblock->log_block_size) * inode_f->block[0]);
					unsigned int file_offset;
					for (file_offset = 0; file_offset < inode_f->size; ++file_offset) {
						kprintf("%c", file_pointer[file_offset]);
					}
				}

				free(name);
				dir_offset += d_ent->rec_len;
				if (d_ent->inode == 0)
					break;
			}
			break;
		}
		kprintf("\n");
	};
#endif

	return 0;
}
