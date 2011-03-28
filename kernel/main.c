/*
 * vim:tabstop=4
 * vim:noexpandtab
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
#include <boot.h>
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
int main(struct multiboot *mboot_ptr, uint32_t mboot_mag, uintptr_t esp)
{
	initial_esp = esp;
	enum BOOTMODE boot_mode = unknown; /* Boot Mode */
	char * ramdisk = NULL;
	if (mboot_mag == MULTIBOOT_EAX_MAGIC) {
		/*
		 * Multiboot (GRUB, native QEMU, PXE)
		 */
		boot_mode = multiboot;
		/*
		 * Realign memory to the end of the multiboot modules
		 */
		kmalloc_startat(0x200000);
		if (mboot_ptr->flags & (1 << 3)) {
			/*
			 * Mboot modules are available.
			 */
			if (mboot_ptr->mods_count > 0) {
				/*
				 * Ramdisk image was provided. (hopefully)
				 */
				uint32_t module_start = *((uint32_t *) mboot_ptr->mods_addr);		/* Start address */
				uint32_t module_end = *(uint32_t *) (mboot_ptr->mods_addr + 4);		/* End address */
				ramdisk = (char *)kmalloc(module_end - module_start);				/* New chunk of ram for it. */
				memcpy(ramdisk, (char *)module_start, module_end - module_start);	/* Copy it over. */
			}
		}
	} else {
		/*
		 * This isn't a multiboot attempt. We were probably loaded by
		 * Mr. Boots, our dedicated boot loader. Verify this...
		 */
		boot_mode = mrboots;
	}

	/* Initialize core modules */
	gdt_install();		/* Global descriptor table */
	idt_install();		/* IDT */
	isrs_install();		/* Interrupt service requests */
	irq_install();		/* Hardware interrupt requests */
	init_video();		/* VGA driver */

	/* Hardware drivers */
	timer_install();	/* PIC driver */
	keyboard_install();	/* Keyboard interrupt handler */
	serial_install();	/* Serial console */

	/* Memory management */
	paging_install(mboot_ptr->mem_upper);	/* Paging */
	heap_install();							/* Kernel heap */
	tasking_install();						/* Multi-tasking */

	/* Kernel Version */
	settextcolor(12, 0);
	kprintf("[%s %s]\n", KERNEL_UNAME, KERNEL_VERSION_STRING);

	if (boot_mode == multiboot) {
		/* Print multiboot information */
		dump_multiboot(mboot_ptr);

		if (mboot_ptr->flags & (1 << 3)) {
			/*
			 * If we have an initial ramdisk, mount it.
			 */
			if (mboot_ptr->mods_count > 0) {
				initrd_mount((uintptr_t)ramdisk, 0);
			}
		}

		/* Parse the command-line arguments */
		parse_args((char *)mboot_ptr->cmdline);
	}

	/*
	 * All the output we just dumped went to the serial line.
	 * If you really want it that bad, feel free to remove
	 * the clear before the rest of the boot process starts.
	 *
	 * But really, the kernel shouldn't be outputting all 
	 * sorts of random text on boot up. Only important stuff!
	 */
	cls();

	/*
	 * Aw man...
	 */
	fork();

	if (getpid() == 0) {
		while (1) {
			uint16_t hours, minutes, seconds;
			get_time(&hours, &minutes, &seconds);

			__asm__ __volatile__ ("cli");
			/* It would help a lot if I had %.2d */
			/* kprintf("[%.2d:%.2d:%.2d]", hours, minutes, seconds); */
			if (bochs_resolution_x) {
				bochs_write_number(hours   / 10, bochs_resolution_x - 8 * 8, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(hours   % 10, bochs_resolution_x - 8 * 7, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(          10, bochs_resolution_x - 8 * 6, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(minutes / 10, bochs_resolution_x - 8 * 5, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(minutes % 10, bochs_resolution_x - 8 * 4, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(          10, bochs_resolution_x - 8 * 3, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(seconds / 10, bochs_resolution_x - 8 * 2, 0, 0x00FFFFFF, 0x0);
				bochs_write_number(seconds % 10, bochs_resolution_x - 8 * 1, 0, 0x00FFFFFF, 0x0);
			} else {
				store_csr();
				set_serial(0);
				set_csr(0);
				place_csr(70,0);
				writech('[');
				kprintf("%d", hours   / 10);
				kprintf("%d", hours   % 10);
				writech(':');
				kprintf("%d", minutes / 10);
				kprintf("%d", minutes % 10);
				writech(':');
				kprintf("%d", seconds / 10);
				kprintf("%d", seconds % 10);
				writech(']');
				restore_csr();
			}
			__asm__ __volatile__ ("sti");
			__asm__ __volatile__ ("hlt");
		}
	} else {
		start_shell();
	}

	return 0;
}
