#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TRACE_APP_NAME "ld.so"

#include "../kernel/include/elf.h"
#include "../userspace/lib/trace.h"


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

	TRACE("section headers are at %p", main.header.e_shoff);
	TRACE("section headers are 0x%x bytes", main.header.e_shentsize);
	TRACE("there are %d section headers", main.header.e_shnum);


	return 0;
}
