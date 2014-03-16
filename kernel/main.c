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
#include <args.h>
#include <module.h>

uintptr_t initial_esp = 0;

/*
 * multiboot i386 (pc) kernel entry point
 */
int kmain(struct multiboot *mboot, uint32_t mboot_mag, uintptr_t esp) {
	initial_esp = esp;
	extern char * cmdline;

	uint32_t mboot_mods_count = 0;
	mboot_mod_t * mboot_mods = NULL;

	if (mboot_mag == MULTIBOOT_EAX_MAGIC) {
		/* Multiboot (GRUB, native QEMU, PXE) */
		debug_print(NOTICE, "Relocating Multiboot structures...");

		mboot_ptr = mboot;

		char cmdline_[1024];

		if (mboot_ptr->vbe_mode_info) {
			lfb_vid_memory = (uint8_t *)((vbe_info_t *)(mboot_ptr->vbe_mode_info))->physbase;
		}

		if (mboot_ptr->flags & (1 << 3)) {
			debug_print(NOTICE, "There %s %d module%s starting at 0x%x.", mboot_ptr->mods_count == 1 ? "is" : "are", mboot_ptr->mods_count, mboot_ptr->mods_count == 1 ? "" : "s", mboot_ptr->mods_addr);
			debug_print(NOTICE, "Current kernel heap start point would be 0x%x.", &end);
			if (mboot_ptr->mods_count > 0) {
				uintptr_t last_mod = (uintptr_t)&end;
				uint32_t i;
				mboot_mods = (mboot_mod_t *)mboot_ptr->mods_addr;
				mboot_mods_count = mboot_ptr->mods_count;
				for (i = 0; i < mboot_ptr->mods_count; ++i ) {
					mboot_mod_t * mod = &mboot_mods[i];
					uint32_t module_start = mod->mod_start;
					uint32_t module_end   = mod->mod_end;
					if ((uintptr_t)mod + sizeof(mboot_mod_t) > last_mod) {
						/* Just in case some silly person put this *behind* the modules... */
						last_mod = (uintptr_t)mod + sizeof(mboot_mod_t);
					}
					debug_print(NOTICE, "Module %d is at 0x%x:0x%x", i, module_start, module_end);
					if (last_mod < module_end) {
						last_mod = module_end;
					}
				}
				debug_print(NOTICE, "Moving kernel heap start to 0x%x\n", last_mod);
				kmalloc_startat(last_mod);
			}
		}

		size_t len = strlen((char *)mboot_ptr->cmdline);
		memmove(cmdline_, (char *)mboot_ptr->cmdline, len + 1);

		/* Relocate the command line */
		cmdline = (char *)kmalloc(len + 1);
		memcpy(cmdline, cmdline_, len + 1);
	}

	/* Initialize core modules */
	gdt_install();      /* Global descriptor table */
	idt_install();      /* IDT */
	isrs_install();     /* Interrupt service requests */
	irq_install();      /* Hardware interrupt requests */

	/* Memory management */
	paging_install(mboot_ptr->mem_upper + mboot_ptr->mem_lower);
	heap_install();     /* Kernel heap */

	if (cmdline) {
		args_parse(cmdline);
	}

	vfs_install();

	/* Hardware drivers */
	timer_install();    /* PIC driver */
	tasking_install();  /* Multi-tasking */
	fpu_install();      /* FPU/SSE magic */
	syscalls_install(); /* Install the system calls */
	shm_install();      /* Install shared memory */
	modules_install();  /* Modules! */

	/* This stuff can probably be made modules once we have better interfaces */
	keyboard_install(); /* Keyboard interrupt handler */
	mouse_install();    /* Mouse driver */
	keyboard_reset_ps2();

	/* This stuff NEEDS to become modules... */
	serial_mount_devices();

	vfs_mount("/dev/null", null_device_create());
	vfs_mount("/dev/zero", zero_device_create());
	vfs_mount("/dev/random", random_device_create());
	vfs_mount("/tmp", tmpfs_create());
	vfs_mount("/home/local", tmpfs_create());

	debug_print_vfs_tree();

	legacy_parse_args();

	if (!fs_root) {
		vfs_mount("/", tmpfs_create());
	}

	/* Load modules from bootloader */
	debug_print(NOTICE, "%d modules to load", mboot_mods_count);
	for (unsigned int i = 0; i < mboot_ptr->mods_count; ++i ) {
		mboot_mod_t * mod = &mboot_mods[i];
		uint32_t module_start = mod->mod_start;
		debug_print(NOTICE, "Loading a module: 0x%x:0x%x", module_start);
		module_defs * mod_info = (module_defs *)module_load_direct((void *)(module_start));
		debug_print(NOTICE, "Loaded: %s", mod_info->name);
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

