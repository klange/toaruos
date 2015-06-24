/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * The ToAruOS kernel is released under the terms of the
 * University of Illinois / NCSA License.
 *
 * Copyright (C) 2011-2014 Kevin Lange.  All rights reserved.
 * Copyright (C) 2012 Markus Schober
 * Copyright (C) 2014 Lioncash
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

fs_node_t * ramdisk_mount(uintptr_t, size_t);

#ifdef EARLY_BOOT_LOG
#define EARLY_LOG_DEVICE 0x3F8
static uint32_t _early_log_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		outportb(EARLY_LOG_DEVICE, buffer[i]);
	}
	return size;
}
fs_node_t _early_log = { .write = &_early_log_write };
#define ENABLE_EARLY_BOOT_LOG(level) do { debug_file = &_early_log; debug_level = (level); } while (0)
#define DISABLE_EARLY_BOOT_LOG() do { debug_file = NULL; debug_level = NOTICE; } while (0)
#else
#define ENABLE_EARLY_BOOT_LOG(level)
#define DISABLE_EARLY_BOOT_LOG()
#endif

struct pack_header {
	char     head[4];
	uint32_t region_size;
};

/*
 * multiboot i386 (pc) kernel entry point
 */
int kmain(struct multiboot *mboot, uint32_t mboot_mag, uintptr_t esp) {
	initial_esp = esp;
	extern char * cmdline;

	uint32_t mboot_mods_count = 0;
	mboot_mod_t * mboot_mods = NULL;
	mboot_ptr = mboot;

	ENABLE_EARLY_BOOT_LOG(0);

	assert(mboot_mag == MULTIBOOT_EAX_MAGIC && "Didn't boot with multiboot, not sure how we got here.");
	debug_print(NOTICE, "Processing Multiboot information.");

	/* Initialize core modules */
	gdt_install();      /* Global descriptor table */
	idt_install();      /* IDT */
	isrs_install();     /* Interrupt service requests */
	irq_install();      /* Hardware interrupt requests */

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
			debug_print(NOTICE, "Moving kernel heap start to 0x%x", last_mod);
			kmalloc_startat(last_mod);
		}
	}

	paging_install(mboot_ptr->mem_upper + mboot_ptr->mem_lower);
	if (mboot_ptr->flags & (1 << 6)) {
		debug_print(NOTICE, "Parsing memory map.");
		mboot_memmap_t * mmap = (void *)mboot_ptr->mmap_addr;
		while ((uintptr_t)mmap < mboot_ptr->mmap_addr + mboot_ptr->mmap_length) {
			if (mmap->type == 2) {
				for (unsigned long long int i = 0; i < mmap->length; i += 0x1000) {
					if (mmap->base_addr + i > 0xFFFFFFFF) break; /* xxx */
					debug_print(INFO, "Marking 0x%x", (uint32_t)(mmap->base_addr + i));
					paging_mark_system((mmap->base_addr + i) & 0xFFFFF000);
				}
			}
			mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uintptr_t));
		}
	}
	paging_finalize();

	{
		char cmdline_[1024];

		size_t len = strlen((char *)mboot_ptr->cmdline);
		memmove(cmdline_, (char *)mboot_ptr->cmdline, len + 1);

		/* Relocate the command line */
		cmdline = (char *)kmalloc(len + 1);
		memcpy(cmdline, cmdline_, len + 1);
	}

	/* Memory management */
	heap_install();     /* Kernel heap */

	if (cmdline) {
		args_parse(cmdline);
	}

	vfs_install();
	tasking_install();  /* Multi-tasking */
	timer_install();    /* PIC driver */
	fpu_install();      /* FPU/SSE magic */
	syscalls_install(); /* Install the system calls */
	shm_install();      /* Install shared memory */
	modules_install();  /* Modules! */

	DISABLE_EARLY_BOOT_LOG();

	/* Load modules from bootloader */
	debug_print(NOTICE, "%d modules to load", mboot_mods_count);
	for (unsigned int i = 0; i < mboot_ptr->mods_count; ++i ) {
		mboot_mod_t * mod = &mboot_mods[i];
		uint32_t module_start = mod->mod_start;
		uint32_t module_end = mod->mod_end;
		size_t   module_size = module_end - module_start;

		int check_result = module_quickcheck((void *)module_start);
		if (check_result == 1) {
			debug_print(NOTICE, "Loading a module: 0x%x:0x%x", module_start, module_end);
			module_data_t * mod_info = (module_data_t *)module_load_direct((void *)(module_start), module_size);
			if (mod_info) {
				debug_print(NOTICE, "Loaded: %s", mod_info->mod_info->name);
			}
		} else if (check_result == 2) {
			/* Mod pack */
			debug_print(NOTICE, "Loading modpack. %x", module_start);
			struct pack_header * pack_header = (struct pack_header *)module_start;
			while (pack_header->region_size) {
				void * start = (void *)((uintptr_t)pack_header + 4096);
				int result = module_quickcheck(start);
				if (result != 1) {
					debug_print(WARNING, "Not actually a module?! %x", start);
				}
				module_data_t * mod_info = (module_data_t *)module_load_direct(start, pack_header->region_size);
				if (mod_info) {
					debug_print(NOTICE, "Loaded: %s", mod_info->mod_info->name);
				}
				pack_header = (struct pack_header *)((uintptr_t)start + pack_header->region_size);
			}
			debug_print(NOTICE, "Done with modpack.");
		} else {
			debug_print(NOTICE, "Loading ramdisk: 0x%x:0x%x", module_start, module_end);
			ramdisk_mount(module_start, module_size);
		}
	}

	/* Map /dev to a device mapper */
	map_vfs_directory("/dev");

	if (args_present("root")) {
		vfs_mount_type("ext2", args_value("root"), "/");
	}

	if (args_present("start")) {
		char * c = args_value("start");
		if (!c) {
			debug_print(WARNING, "Expected an argument to kernel option `start`. Ignoring.");
		} else {
			debug_print(NOTICE, "Got start argument: %s", c);
			boot_arg = strdup(c);
		}
	}

	if (!fs_root) {
		debug_print(CRITICAL, "No root filesystem is mounted. Skipping init.");
		map_vfs_directory("/");
		switch_task(0);
	}

	/* Prepare to run /bin/init */
	char * argv[] = {
		"/bin/init",
		boot_arg,
		NULL
	};
	int argc = 0;
	while (argv[argc]) {
		argc++;
	}
	system(argv[0], argc, argv); /* Run init */

	return 0;
}

