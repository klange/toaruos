/*
 * ELF Static Executable Loader
 *
 * Part of the ToAru OS Kernel
 * (C) 2011 Kevin Lange
 * Released under the terms of the NCSA License, see the included
 * README file for further information.
 */

#include <system.h>
#include <fs.h>
#include <elf.h>

/**
 * Load and execute a static ELF binary.
 *
 * We make one assumption on the location the binary expects to be loaded
 * at: that it be outside of the kernel memory space.
 *
 * Arguments are passed to the stack of the user application so that they
 * can be read properly.
 *
 * TODO: Environment variables should be loaded somewhere.
 *
 * HACK: ELF verification isn't complete.
 *
 * @param path Path to the executable to attempt to execute.
 * @param argc Number of arguments (because I'm not counting for you)
 * @param argv Pointer to a string of arguments
 */
int
exec(
		char *  path, /* Path to the executable to run */
		int     argc, /* Argument count (ie, /bin/echo hello world = 3) */
		char ** argv  /* Argument strings (including executable path) */
	) {
	/* Open the file */
	fs_node_t * file = kopen(path,0);
	if (!file) {
		/* Command not found */
		return 0;
	}
	/* Read in the binary contents */
	Elf32_Header * header = (Elf32_Header *)malloc(file->length + 100);
	read_fs(file, 0, file->length, (uint8_t *)header);

	/* Alright, we've read the binary, time to load the loadable sections */
	/* Verify the magic */
	if (	header->e_ident[0] != ELFMAG0 ||
			header->e_ident[1] != ELFMAG1 ||
			header->e_ident[2] != ELFMAG2 ||
			header->e_ident[3] != ELFMAG3) {
		/* What? This isn't an ELF... */
		kprintf("Fatal: Not a valid ELF executable.\n");
		free(header);
		close_fs(file);
		return -1;
	}

	/* Load the loadable segments from the binary */
	for (uintptr_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		/* read a section header */
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)header + (header->e_shoff + x));
		if (shdr->sh_addr) {
			/* If this is a loadable section, load it up. */
			if (shdr->sh_addr < current_task->entry) {
				/* If this is the lowest entry point, store it for memory reasons */
				current_task->entry = shdr->sh_addr;
			}
			if (shdr->sh_addr + shdr->sh_size - current_task->entry > current_task->image_size) {
				/* We also store the total size of the memory region used by the application */
				current_task->image_size = shdr->sh_addr + shdr->sh_size - current_task->entry;
			}
			for (uintptr_t i = 0; i < shdr->sh_size + 0x5000; i += 0x1000) {
				/* This doesn't care if we already allocated this page */
				alloc_frame(get_page(shdr->sh_addr + i, 1, current_directory), 0, 1);
			}
			if (shdr->sh_type == SHT_NOBITS) {
				/* This is the .bss, zero it */
				memset((void *)(shdr->sh_addr), 0x0, shdr->sh_size);
			} else {
				/* Copy the section into memory */
				memcpy((void *)(shdr->sh_addr), (void *)((uintptr_t)header + shdr->sh_offset), shdr->sh_size);
			}
		}
	}

	/* Store the entry point to the code segment */
	uintptr_t entry = (uintptr_t)header->e_entry;

	/* Free the space we used for the ELF headers and files */
	free(header);
	close_fs(file);

	for (uintptr_t stack_pointer = 0x10000000; stack_pointer < 0x100F0000; stack_pointer += 0x1000) {
		alloc_frame(get_page(stack_pointer, 1, current_directory), 0, 1);
	}

	uintptr_t heap = current_task->entry + current_task->image_size;
	alloc_frame(get_page(heap, 1, current_directory), 0, 1);
	char ** argv_ = (char **)heap;
	heap += sizeof(char *) * argc;
	for (int i = 0; i < argc; ++i) {
		alloc_frame(get_page(heap, 1, current_directory), 0, 1);
		argv_[i] = (char *)heap;
		memcpy((void *)heap, argv[i], strlen(argv[i]) * sizeof(char) + 1);
		heap += strlen(argv[i]) + 1;
	}

	current_task->heap   = heap; /* heap end */
	current_task->heap_a = heap + (0x1000 - heap % 0x1000);
	current_task->stack  = 0x100F0000;
	current_task->next_fd = 3;

	/* Go go go */
	enter_user_jmp(entry, argc, argv_, 0x100EFFFF);

		/* We should never reach this code */
	return -1;
}

int
system(
		char *  path, /* Path to the executable to run */
		int     argc, /* Argument count (ie, /bin/echo hello world = 3) */
		char ** argv  /* Argument strings (including executable path) */
	) {
	int child = fork();
	if (child == 0) {
		exec(path,argc,argv);
		kexit(-1);
		return -1;
	} else {
		/* We are system(), so we need to wait for the child
		 * application to exit before we can continue. */
		/* Get the child task. */
		task_t * volatile child_task = gettask(child);
		/* If the child task doesn't exist, bail */
		if (!child_task) return -1;
		/* Wait until it finishes (this is stupidly memory intensive,
		 * but we haven't actually implemented wait() yet, so there's
		 * not all that much we can do right now. */
		while (child_task->finished == 0) {
			if (child_task->finished != 0) break;
		}
		/* Grab the child's return value */
		return child_task->retval;
	}
}

/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
