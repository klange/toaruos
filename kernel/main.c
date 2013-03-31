/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * The ToAruOS kernel is released under the terms of the
 * University of Illinois / NCSA License.
 *
 * Copyright (c) 2011-2013 Kevin Lange.  All rights reserved.
 *
 *                           Dedicated to the memory of
 *                                Dennis Ritchie
 *                                   1941-2011
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
#include <boot.h>
#include <ext2.h>
#include <fs.h>
#include <logging.h>
#include <process.h>
#include <shm.h>

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
int main(struct multiboot *mboot, uint32_t mboot_mag, uintptr_t esp) {
	initial_esp = esp;
	uintptr_t ramdisk_top = 0;
	extern char * cmdline;


	if (mboot_mag == MULTIBOOT_EAX_MAGIC) {
		/* Multiboot (GRUB, native QEMU, PXE) */
		debug_print(NOTICE, "Relocating Multiboot structures...");
		mboot_ptr = mboot;
		char cmdline_[1024];

		if (mboot_ptr->vbe_mode_info) {
			lfb_vid_memory = (uint8_t *)((vbe_info_t *)(mboot_ptr->vbe_mode_info))->physbase;
		}

		size_t len = strlen((char *)mboot_ptr->cmdline);
		memmove(cmdline_, (char *)mboot_ptr->cmdline, len + 1);

		/* Relocate any available modules */
		if (mboot_ptr->flags & (1 << 3)) {
			if (mboot_ptr->mods_count > 0) {
				/*
				 * Ramdisk image was provided. (hopefully)
				 */
				uint32_t module_start = *((uint32_t *) mboot_ptr->mods_addr);		/* Start address */
				uint32_t module_end = *(uint32_t *) (mboot_ptr->mods_addr + 4);		/* End address */
				ramdisk = (char *)0xD0000000; //(char *)kmalloc(module_end - module_start);
				ramdisk_top = (uintptr_t)ramdisk + (module_end - module_start);

				memmove(ramdisk, (char *)module_start, module_end - module_start);	/* Copy it over. */
			}
		}

		/* Relocate the command line */
		cmdline = (char *)kmalloc(len + 1);
		memcpy(cmdline, cmdline_, len + 1);
	}

	/* Initialize core modules */
	gdt_install();		/* Global descriptor table */
	idt_install();		/* IDT */
	isrs_install();		/* Interrupt service requests */
	irq_install();		/* Hardware interrupt requests */

	/* Memory management */
	paging_install(mboot_ptr->mem_upper + mboot_ptr->mem_lower);	/* Paging */
	heap_install();							/* Kernel heap */

	vfs_install();

	/* Hardware drivers */
	timer_install();	/* PIC driver */
	serial_install();	/* Serial console */

	tasking_install();	/* Multi-tasking */
	enable_fpu();		/* Enable the floating point unit */
	syscalls_install();	/* Install the system calls */

	shm_install();		/* Install shared memory */

	keyboard_install();	/* Keyboard interrupt handler */
	mouse_install();	/* Mouse driver */
	keyboard_reset_ps2();

	if (cmdline) {
		parse_args(cmdline);
	}

	/*
	vfs_mount("/foo/bar/baz", fs_root);
	vfs_mount("/foo/bar/loof", fs_root);
	vfs_mount("/raz/daz/bar", fs_root);
	*/

	serial_mount_devices();
	vfs_mount("/dev/null", null_device_create());
	vfs_mount("/dev/zero", zero_device_create());
	vfs_mount("/dev/hello", hello_device_create());
	vfs_mount("/dev/random", random_device_create());

	vfs_mount("/proc", procfs_create());

	debug_print_vfs_tree();

	//assert(0);

	if (ramdisk && !fs_root) {
		kprintf("---- ramdisk[0x%x:0x%x]\n", ramdisk, ramdisk_top);
		for (uintptr_t i = (uintptr_t)ramdisk; i <= (uintptr_t)ramdisk_top; i += 0x1000) {
			dma_frame(get_page(i, 1, kernel_directory), 0, 1, i);
		}

		ext2_ramdisk_mount((uintptr_t)ramdisk);
	}

	if (!fs_root) {
		debug_print(CRITICAL, "There is no file system mounted.");
		debug_print(CRITICAL, "You have done something wrong;");
		debug_print(CRITICAL, "Did you forget to mount a hard disk?");
		while (1) {
		}
	}

	/* Prepare to run /bin/init */
	char * argv[] = {
		"/bin/init",
		boot_arg,
		boot_arg_extra,
		NULL
	};
	int argc = 0;
	while (argv[argc]) {
		argc++;
	}
	system(argv[0], argc, argv); /* Run init */

	return 0;
}

