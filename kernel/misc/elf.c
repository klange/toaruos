/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * ELF Static Executable Loader
 *
 */

#include <system.h>
#include <fs.h>
#include <elf.h>
#include <process.h>
#include <logging.h>

int exec_elf(char * path, fs_node_t * file, int argc, char ** argv, char ** env) {
	Elf32_Header * header = (Elf32_Header *)malloc(file->length + 100);

	debug_print(NOTICE, "---> Starting load.");
	IRQ_RES;
	read_fs(file, 0, file->length, (uint8_t *)header);
	IRQ_OFF;
	debug_print(NOTICE, "---> Finished load.");

	current_process->name = malloc(strlen(path) + 1);
	memcpy(current_process->name, path, strlen(path) + 1);

	current_process->cmdline = argv;

	/* Alright, we've read the binary, time to load the loadable sections */
	/* Verify the magic */
	if (	header->e_ident[0] != ELFMAG0 ||
			header->e_ident[1] != ELFMAG1 ||
			header->e_ident[2] != ELFMAG2 ||
			header->e_ident[3] != ELFMAG3) {
		/* What? This isn't an ELF... */
		debug_print(ERROR, "Not a valid ELF executable.");
		free(header);
		close_fs(file);
		return -1;
	}

	release_directory_for_exec(current_directory);
	invalidate_page_tables();

	current_process->image.entry = 0xFFFFFFFF;

	/* Load the loadable segments from the binary */
	for (uintptr_t x = 0; x < (uint32_t)header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		/* read a section header */
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)header + (header->e_shoff + x));
		if (shdr->sh_addr) {
			/* If this is a loadable section, load it up. */
			if (shdr->sh_addr == 0) continue; /* skip sections that try to load to 0 */
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
				invalidate_tables_at(shdr->sh_addr + i);
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

	for (uintptr_t stack_pointer = USER_STACK_BOTTOM; stack_pointer < USER_STACK_TOP; stack_pointer += 0x1000) {
		alloc_frame(get_page(stack_pointer, 1, current_directory), 0, 1);
		invalidate_tables_at(stack_pointer);
	}

	/* Collect arguments */
	int envc = 0;
	for (envc = 0; env[envc] != NULL; ++envc);

	/* Format auxv */
	Elf32_auxv auxv[] = {
		{256, 0xDEADBEEF},
		{0, 0}
	};
	int auxvc = 0;
	for (auxvc = 0; auxv[auxvc].id != 0; ++auxvc);
	auxvc++;

	uintptr_t heap = current_process->image.entry + current_process->image.size;
	alloc_frame(get_page(heap, 1, current_directory), 0, 1);
	invalidate_tables_at(heap);
	char ** argv_ = (char **)heap;
	heap += sizeof(char *) * (argc + 1);
	char ** env_ = (char **)heap;
	heap += sizeof(char *) * (envc + 1);
	void * auxv_ptr = (void *)heap;
	heap += sizeof(Elf32_auxv) * (auxvc);

	for (int i = 0; i < argc; ++i) {
		size_t size = strlen(argv[i]) * sizeof(char) + 1;
		for (uintptr_t x = heap; x < heap + size + 0x1000; x += 0x1000) {
			alloc_frame(get_page(x, 1, current_directory), 0, 1);
		}
		invalidate_tables_at(heap);
		argv_[i] = (char *)heap;
		memcpy((void *)heap, argv[i], size);
		heap += size;
	}
	/* Don't forget the NULL at the end of that... */
	argv_[argc] = 0;

	for (int i = 0; i < envc; ++i) {
		size_t size = strlen(env[i]) * sizeof(char) + 1;
		for (uintptr_t x = heap; x < heap + size + 0x1000; x += 0x1000) {
			alloc_frame(get_page(x, 1, current_directory), 0, 1);
		}
		invalidate_tables_at(heap);
		env_[i] = (char *)heap;
		memcpy((void *)heap, env[i], size);
		heap += size;
	}
	env_[envc] = 0;

	memcpy(auxv_ptr, auxv, sizeof(Elf32_auxv) * (auxvc));

	current_process->image.heap        = heap; /* heap end */
	current_process->image.heap_actual = heap + (0x1000 - heap % 0x1000);
	alloc_frame(get_page(current_process->image.heap_actual, 1, current_directory), 0, 1);
	invalidate_tables_at(current_process->image.heap_actual);
	current_process->image.user_stack  = USER_STACK_TOP;

	current_process->image.start = entry;

	/* XXX setuid */
	if (file->mask & 0x800) {
		debug_print(WARNING, "setuid binary executed [%s, uid:%d]", file->name, file->uid);
		current_process->user = file->uid;
	}

	close_fs(file);

	/* Go go go */
	enter_user_jmp(entry, argc, argv_, USER_STACK_TOP);

	/* We should never reach this code */
	return -1;
}

int exec_shebang(char * path, fs_node_t * file, int argc, char ** argv, char ** env) {
	/* Read MAX_LINE... */
	char tmp[100];
	read_fs(file, 0, 100, (unsigned char *)tmp); close_fs(file);
	char * cmd = (char *)&tmp[2];
	char * space_or_linefeed = strpbrk(cmd, " \n");
	char * arg = NULL;

	if (!space_or_linefeed) {
		debug_print(WARNING, "No space or linefeed found.");
		return -ENOEXEC;
	}

	if (*space_or_linefeed == ' ') {
		/* Oh lovely, an argument */
		*space_or_linefeed = '\0';
		space_or_linefeed++;
		arg = space_or_linefeed;
		space_or_linefeed = strpbrk(space_or_linefeed, "\n");
		if (!space_or_linefeed) {
			debug_print(WARNING, "Argument exceeded maximum length");
			return -ENOEXEC;
		}
	}
	*space_or_linefeed = '\0';

	char script[strlen(path)+1];
	memcpy(script, path, strlen(path)+1);

	unsigned int nargc = argc + (arg ? 2 : 1);
	char * args[nargc];
	args[0] = cmd;
	args[1] = arg ? arg : script;
	args[2] = arg ? script : NULL;
	args[3] = NULL;

	int j = arg ? 3 : 2;
	for (int i = 1; i < argc; ++i, ++j) {
		args[j] = argv[i];
	}
	args[j] = NULL;

	return exec(cmd, nargc, args, env);
}

/* Consider exposing this and making it a list so it can be extended ... */
typedef int (*exec_func)(char * path, fs_node_t * file, int argc, char ** argv, char ** env);
typedef struct {
	exec_func func;
	unsigned char bytes[4];
	unsigned int  match;
	char * name;
} exec_def_t;

exec_def_t fmts[] = {
	{exec_elf, {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3}, 4, "ELF"},
	{exec_shebang, {'#', '!', 0, 0}, 2, "#!"},
};

static int matches(unsigned char * a, unsigned char * b, unsigned int len) {
	for (unsigned int i = 0; i < len; ++i) {
		if (a[i] != b[i]) return 0;
	}
	return 1;
}

/**
 * Load an execute a binary.
 *
 * This determines the binary type (eg., ELF binary, she-bang script, etc.)
 * and then calls the appropriate underlying exec function.
 *
 * @param path Path to the executable to attempt to execute.
 * @param argc Number of arguments (because I'm not counting for you)
 * @param argv Pointer to a string of arguments
 */
int exec(
		char *  path, /* Path to the executable to run */
		int     argc, /* Argument count (ie, /bin/echo hello world = 3) */
		char ** argv, /* Argument strings (including executable path) */
		char ** env   /* Environmen variables */
	) {
	/* Open the file */
	fs_node_t * file = kopen(path,0);
	if (!file) {
		/* Command not found */
		return -ENOENT;
	}

	/* Read four bytes of the file */
	unsigned char head[4];
	read_fs(file, 0, 4, head);

	debug_print(WARNING, "First four bytes: %c%c%c%c", head[0], head[1], head[2], head[3]);

	for (unsigned int i = 0; i < sizeof(fmts) / sizeof(exec_def_t); ++i) {
		if (matches(fmts[i].bytes, head, fmts[i].match)) {
			debug_print(WARNING, "Matched executor: %s", fmts[i].name);
			return fmts[i].func(path, file, argc, argv, env);
		}
	}

	debug_print(WARNING, "Exec failed?");
	return -ENOEXEC;
}


int
system(
		char *  path, /* Path to the executable to run */
		int     argc, /* Argument count (ie, /bin/echo hello world = 3) */
		char ** argv  /* Argument strings (including executable path) */
	) {
	char ** argv_ = malloc(sizeof(char *) * (argc + 1));
	for (int j = 0; j < argc; ++j) {
		argv_[j] = malloc((strlen(argv[j]) + 1) * sizeof(char));
		memcpy(argv_[j], argv[j], strlen(argv[j]) + 1);
	}
	argv_[argc] = 0;
	char * env[] = {NULL};
	set_process_environment((process_t*)current_process, clone_directory(current_directory));
	current_directory = current_process->thread.page_directory;
	switch_page_directory(current_directory);
	exec(path,argc,argv_,env);
	debug_print(ERROR, "Failed to execute process!");
	kexit(-1);
	return -1;
}

