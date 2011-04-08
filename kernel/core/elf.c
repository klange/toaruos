/*
 * ELF Executable Loader
 */

#include <system.h>
#include <fs.h>
#include <elf.h>

static void
a_task(int argc, char ** argv) {
	syscall_print("Hello world!\n");
	syscall_print("I am: (");
	char blarg[2] = { '0' + argc, 0};
	syscall_print(blarg);
	syscall_print(") ");
	syscall_print(argv[0]);
	syscall_print("\n");
	syscall_exit(2);
}

/**
 * Load and execute.
 * @param path Path to the executable to attempt to execute.
 * @param argc Number of arguments (because I'm not counting for you)
 * @param argv Pointer to a string of arguments
 */
int
exec(
		char *  path,
		int     argc,
		char ** argv
	) {
	/* Load the requested file */
	int child = fork();
	if (child == 0) {
		/* Read in the binary */
		fs_node_t * file = kopen(path,0);
		if (!file) {
			/* Command not found */
			kexit(127);
		}
		Elf32_Header * header = (Elf32_Header *)malloc(file->length);
		size_t bytes_read = read_fs(file, 0, file->length, (uint8_t *)header);

		/* Alright, we've read the binary, time to load the loadable sections */
		/* Verify the magic */
		if (	header->e_ident[0] != ELFMAG0 ||
				header->e_ident[1] != ELFMAG1 ||
				header->e_ident[2] != ELFMAG2 ||
				header->e_ident[3] != ELFMAG3) {
			kprintf("Fatal: Not a valid ELF executable.\n");
			kexit(127);
		}

		for (uintptr_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
			Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)header + (header->e_shoff + x));
			if (shdr->sh_addr) {
				for (uintptr_t i = 0; i < shdr->sh_size; i += 0x1000) {
					/* This doesn't care if we already allocated this page */
					alloc_frame(get_page(shdr->sh_addr + i, 1, current_directory), 0, 1);
				}
				memcpy((void *)(shdr->sh_addr), (void *)((uintptr_t)header + shdr->sh_offset), shdr->sh_size);
			}
		}

		enter_user_jmp((uintptr_t)header->e_entry, argc, argv);




		/* We should never reach this mode */
		kexit(0x5ADFACE);
	} else {
		/* You can wait here if you want... */
		task_t * volatile child_task = gettask(child);
		while (child_task->finished == 0) {
			if (child_task->finished != 0) break;
		}
		return child_task->retval;
	}
	return -1;
}



/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
