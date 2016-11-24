#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <alloca.h>
#include <unistd.h>
#include <syscall.h>

#define TRACE_APP_NAME "ld.so"

#include "../kernel/include/elf.h"
#include "../userspace/lib/trace.h"

typedef int (*entry_point_t)(int, char *[], char**);

extern char end[];

struct elf_object {
	FILE * file;
	Elf32_Header header;
};

int main(int argc, char * argv[]) {
	TRACE("Linker end address is %p", &end);
	TRACE("Linking main object: %s", argv[1]);

	struct elf_object main;

	main.file = fopen(argv[1],"r");

	size_t r = fread(&main.header, sizeof(Elf32_Header), 1, main.file);
	TRACE("ELF header from main object read");

	if (main.header.e_ident[0] != ELFMAG0 ||
		main.header.e_ident[1] != ELFMAG1 ||
		main.header.e_ident[2] != ELFMAG2 ||
		main.header.e_ident[3] != ELFMAG3) {
		TRACE("Bad header?");
		return 1;
	}

	switch (main.header.e_type) {
		case ET_EXEC:
			TRACE("ELF executable; continuing to load.");
			break;
		case ET_NONE:
		case ET_REL:
		case ET_DYN:
		case ET_CORE:
			TRACE("ELF file, but not an executable.");
		default:
			TRACE("Not an ELF executable. Bailing!");
			return 1;
	}

	if (main.header.e_machine != EM_386) {
		TRACE("ELF object has wrong machine type; expected EM_386.");
		return 1;
	}

	TRACE("main entry point is at %p", main.header.e_entry);

	TRACE("program headers are at %p", main.header.e_phoff);
	TRACE("program headers are 0x%x bytes", main.header.e_phentsize);
	TRACE("there are %d program headers", main.header.e_phnum);

	uintptr_t latest_heap = (uintptr_t)sbrk(0);

	{

		size_t headers = 0;
		Elf32_Phdr *phdr = alloca(main.header.e_phentsize);
		while (headers < main.header.e_phnum) {
			fseek(main.file, main.header.e_phoff + main.header.e_phentsize * headers, SEEK_SET);
			fread(phdr, main.header.e_phentsize, 1, main.file);

			/* Print the header type */
			switch (phdr->p_type) {
				case PT_LOAD:
#if 1
					TRACE("[Loadable Segment]");
					TRACE("  offse: 0x%x", phdr->p_offset);
					TRACE("  vaddr: 0x%x", phdr->p_vaddr);
					TRACE("  paddr: 0x%x", phdr->p_paddr);
					TRACE("  files: 0x%x", phdr->p_filesz);
					TRACE("  memsz: 0x%x", phdr->p_memsz);
					TRACE("  align: 0x%x", phdr->p_align);
#endif
					/* Load section */
					{
						/* Replace this with some sort of mmap */
						uintptr_t current_heap = (uintptr_t)sbrk(0);
						char * args[] = {(char*)phdr->p_vaddr};
						syscall_system_function(9, args);
						uintptr_t s = (uintptr_t)sbrk(phdr->p_memsz) + phdr->p_memsz;
						if (s > latest_heap) {
							latest_heap = s;
							while (latest_heap % 0x1000) latest_heap++;
						}
						fseek(main.file, phdr->p_offset, SEEK_SET);
						fread((void*)phdr->p_vaddr, phdr->p_filesz, 1, main.file);
						size_t read = phdr->p_filesz;
						while (read < phdr->p_memsz) {
							*(char *)(phdr->p_vaddr + read) = 0;
							read++;
						}

						/* Return the heap to its proper location */
						{
							char * args[] = {(char*)current_heap};
							TRACE("Returning heap to 0x%x", current_heap);
							syscall_system_function(9, args);
						}
					}
					break;
#if 1
				case PT_DYNAMIC:
					TRACE("[Dynamic Loading Information]");
					break;
				case PT_INTERP:
					TRACE("[Interpreter Path]");
					break;
				default:
					TRACE("[Unused Segement]");
					break;
#endif
			}

			headers++;
		}
		free(phdr);
	}

	/* Place the heap at the end */
	{
		TRACE("Returning heap to 0x%x", latest_heap);
		char * args[] = {(char*)latest_heap};
		syscall_system_function(9, args);
	}
	entry_point_t entry = (entry_point_t)main.header.e_entry;
	entry(argc-1,argv+1,environ);

	return 0;
}
