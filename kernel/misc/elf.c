/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
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
#include <process.h>
#include <logging.h>

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
	for (uintptr_t x = 0x30000000; x < 0x30000000 + file->length; x += 0x1000) {
		alloc_frame(get_page(x, 1, current_directory), 0, 1);
	}
	Elf32_Header * header = (Elf32_Header *)0x30000000; //(Elf32_Header *)malloc(file->length + 100);
	read_fs(file, 0, file->length, (uint8_t *)header);

	current_process->name = malloc(strlen(path) + 1);
	memcpy(current_process->name, path, strlen(path) + 1);

	/* Alright, we've read the binary, time to load the loadable sections */
	/* Verify the magic */
	if (	header->e_ident[0] != ELFMAG0 ||
			header->e_ident[1] != ELFMAG1 ||
			header->e_ident[2] != ELFMAG2 ||
			header->e_ident[3] != ELFMAG3) {
		/* What? This isn't an ELF... */
		kprintf("Fatal: Not a valid ELF executable.\n");
		for (uintptr_t x = 0x30000000; x < 0x30000000 + file->length; x += 0x1000) {
			free_frame(get_page(x, 0, current_directory));
		}
		close_fs(file);
		return -1;
	}

	/* Load the loadable segments from the binary */
	for (uintptr_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		/* read a section header */
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)header + (header->e_shoff + x));
		if (shdr->sh_addr) {
			/* If this is a loadable section, load it up. */
			if (shdr->sh_addr < current_process->image.entry) {
				/* If this is the lowest entry point, store it for memory reasons */
				current_process->image.entry = shdr->sh_addr;
			}
			if (shdr->sh_addr + shdr->sh_size - current_process->image.entry > current_process->image.size) {
				/* We also store the total size of the memory region used by the application */
				current_process->image.size = shdr->sh_addr + shdr->sh_size - current_process->image.entry;
			}
			for (uintptr_t i = 0; i < shdr->sh_size + 0x2000; i += 0x1000) {
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
	for (uintptr_t x = 0x30000000; x < 0x30000000 + file->length; x += 0x1000) {
		free_frame(get_page(x, 0, current_directory));
	}
	close_fs(file);

	for (uintptr_t stack_pointer = USER_STACK_BOTTOM; stack_pointer < USER_STACK_TOP; stack_pointer += 0x1000) {
		alloc_frame(get_page(stack_pointer, 1, current_directory), 0, 1);
	}

	uintptr_t heap = current_process->image.entry + current_process->image.size;
	alloc_frame(get_page(heap, 1, current_directory), 0, 1);
	char ** argv_ = (char **)heap;
	heap += sizeof(char *) * (argc + 1);
	for (int i = 0; i < argc; ++i) {
		alloc_frame(get_page(heap, 1, current_directory), 0, 1);
		argv_[i] = (char *)heap;
		memcpy((void *)heap, argv[i], strlen(argv[i]) * sizeof(char) + 1);
		heap += strlen(argv[i]) + 1;
	}
	/* Don't forget the NULL at the end of that... */
	argv_[argc] = 0;

	current_process->image.heap        = heap; /* heap end */
	current_process->image.heap_actual = heap + (0x1000 - heap % 0x1000);
	current_process->image.user_stack  = USER_STACK_TOP;
	while (current_process->fds->length < 3) {
		process_append_fd((process_t *)current_process, NULL);
	}

	current_process->image.start = entry;

	/* Go go go */
	enter_user_jmp(entry, argc, argv_, USER_STACK_TOP);

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
		debug_print(ERROR, "Failed to execute process!");
		kexit(-1);
		return -1;
	} else {
		switch_next();
		return -1;
	}
}

