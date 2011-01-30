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
int main(struct multiboot *mboot_ptr)
{

	/* Realing memory to the end of the multiboot modules */
	if (mboot_ptr->mods_count > 0) {
		uint32_t module_start = *((uint32_t *) mboot_ptr->mods_addr);
		uint32_t module_end = *(uint32_t *) (mboot_ptr->mods_addr + 4);
		kmalloc_startat(module_end);
	}
#if 0
	mboot_ptr = copy_multiboot(mboot_ptr);
#endif

	/* Initialize core modules */
	gdt_install();		/* Global descriptor table */
	idt_install();		/* IDT */
	isrs_install();		/* Interrupt service requests */
	irq_install();		/* Hardware interrupt requests */
	init_video();		/* VGA driver */

	/* Hardware drivers */
	timer_install();
	keyboard_install();

	/* Memory management */
	paging_install(mboot_ptr->mem_upper);
	heap_install();

	/* Kernel Version */
	settextcolor(12, 0);
	kprintf("[%s %s]\n", KERNEL_UNAME, KERNEL_VERSION_STRING);

	/* Print multiboot information */
	dump_multiboot(mboot_ptr);

	uint32_t module_start = *((uint32_t *) mboot_ptr->mods_addr);
	uint32_t module_end = *(uint32_t *) (mboot_ptr->mods_addr + 4);

	initrd_mount(module_start, module_end);
	kprintf("Opening /etc/kernel/hello.txt... ");
	fs_node_t *test_file = kopen("/etc/kernel/hello.txt", NULL);
	if (!test_file) {
		kprintf("Couldn't find hello.txt\n");
	}
	kprintf("Found at inode %d\n", test_file->inode);
	char buffer[256];
	uint32_t bytes_read;
	bytes_read = read_fs(test_file, 0, 255, &buffer);
	kprintf("cat /etc/kernel/hello.txt\n");
	uint32_t i = 0;
	for (i = 0; i < bytes_read; ++i) {
		kprintf("%c", buffer[i]);
	}
	close_fs(test_file);
	free(test_file);
	test_file = kopen("/usr/docs/README.md", NULL);
	char *bufferb = malloc(test_file->length + 200);
	bytes_read = read_fs(test_file, 100, test_file->length, bufferb);
	for (i = 0; i < bytes_read; ++i) {
		kprintf("%c", (char)bufferb[i]);
		if (i % 500 == 0) {
			timer_wait(30);
		}
	}
	free(bufferb);
	close_fs(test_file);

	return 0;
}
