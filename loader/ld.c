/*
 * ToAruOS Loader
 * (C) 2011 Kevin Lange
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

/* The Master ELF Header */
#include "../kernel/include/elf.h"

/**
 * Show usage for the readelf application.
 * @param argc Argument count (unused)
 * @param argv Arguments to binary
 */
void usage(int argc, char ** argv) {
	/* Show usage */
	printf("%s [filename]\n", argv[0]);
	printf("Loads a /static/ binary into memory and executes it.\n");
	exit(1);
}

#define   SOURCE        0x02000000
uintptr_t DESTINATION = 0x03000000;

int _main();
int argc;
char ** argv;

/**
 * Application entry point.
 * @returns 0 on sucess, 1 on failure
 */
int main(int argc_, char ** argv_) {
	/* ld stuff */
	uintptr_t END = (uintptr_t)sbrk(0);
	if (END > DESTINATION) {
		printf("Oh. Welcome back. Uh. I'm just going to kill myself now.\n");
		return 2;
	}
	printf("End of memory is 0x%x\n", END);
	printf("We are, therefore, 0x%x bytes long.\n", END - SOURCE);
	printf("I am moving myself to 0x%x", DESTINATION);
	printf("So I want to sbrk(0x%x)\n", DESTINATION + (END - SOURCE) - SOURCE);
	sbrk(DESTINATION + (END - SOURCE) - SOURCE);

	argc = argc_;
	argv = argv_;

	memcpy((void *)DESTINATION, (void *)SOURCE, END - SOURCE);
	printf("Jumping to 0x%x\n", (uintptr_t)&_main - SOURCE + DESTINATION);
	uintptr_t location = ((uintptr_t)&_main - SOURCE + DESTINATION);
	__asm__ __volatile__ ("jmp *%0" : : "m"(location));

	return -1;
}

int _main() {
here:
	printf("Oh, hello.\n");
	printf("0x%x\n", &&here);

	/* Process arguments */
	if (argc < 2) usage(argc,argv);

	FILE * binary;           /**< File pointer for requested executable */
	size_t binary_size;      /**< Size of the file */
	char * binary_buf;       /**< Buffer to store the binary in memory */
	Elf32_Header * header;   /**< ELF header */
	char * string_table[5];  /**< Room for some string tables */

	/* Open the requested binary */
	binary = fopen(argv[1], "r");

	/* Hack because we don't have seek/tell */
	char garbage[3];
	binary_size = 0;
	while (fread((void *)&garbage, 1, 1, binary) != 0) {
		++binary_size;
	}
	printf("Binary is %u bytes.\n", binary_size);
	fclose(binary);
	binary = fopen(argv[1], "r");

	/* Read the binary into a buffer */
	binary_buf = malloc(binary_size);
	fread((void *)binary_buf, binary_size, 1, binary);

	/* Let's start considering this guy an elf, 'eh? */
	header = (Elf32_Header *)binary_buf;

	/* Verify the magic */
	if (	header->e_ident[0] != ELFMAG0 ||
			header->e_ident[1] != ELFMAG1 ||
			header->e_ident[2] != ELFMAG2 ||
			header->e_ident[3] != ELFMAG3) {
		printf("Header magic is wrong!\n");
		printf("Are you sure this is a 32-bit ELF binary or object file?\n");
		return 1;
	}

	/* Let's print out some of the header information, shall we? */
	printf("\033[1mELF Header\033[0m\n");

	/* File type */
	printf("[Type %d] ", header->e_type);
	switch (header->e_type) {
		case ET_NONE:
			printf("No file type.\n");
			break;
		case ET_REL:
			printf("Relocatable file.\n");
			break;
		case ET_EXEC:
			printf("Executable file.\n");
			break;
		case ET_DYN:
			printf("Shared object file.\n");
			break;
		case ET_CORE:
			printf("Core file.\n");
			break;
		default:
			printf("(Unknown file type)\n");
			break;
	}

	/* Machine Type */
	switch (header->e_machine) {
		case EM_386:
			printf("Intel x86\n");
			break;
		default:
			printf("Unknown machine: %d\n", header->e_machine);
			break;
	}

	/* Version == EV_CURRENT? */
	if (header->e_version == EV_CURRENT) {
		printf("ELF version is 1, as it should be.\n");
	}

	/* Entry point in memory */
	printf("Binary entry point in virtual memory is at 0x%x\n", header->e_entry);

	/* Program header table offset */
	printf("Program header table is at +0x%x and one entry is 0x%x bytes.\n"
			"There are %d total program headers.\n",
			header->e_phoff, header->e_phentsize, header->e_phnum);

	/* Section header table offset */
	printf("Section header table is at +0x%x and one entry is 0x%x bytes.\n"
			"There are %d total section headers.\n",
			header->e_shoff, header->e_shentsize, header->e_shnum);

	/* Read the program headers */
	printf("\033[1mProgram Headers\033[0m\n");
	for (uint32_t x = 0; x < header->e_phentsize * header->e_phnum; x += header->e_phentsize) {
		if (header->e_phoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		/* Grab the program header */
		Elf32_Phdr * phdr = (Elf32_Phdr *)((uintptr_t)binary_buf + (header->e_phoff + x));

		/* Print the header type */
		switch (phdr->p_type) {
			case PT_LOAD:
				printf("[Loadable Segment]\n");
				break;
			case PT_DYNAMIC:
				printf("[Dynamic Loading Information]\n");
				break;
			case PT_INTERP:
				printf("[Interpreter Path]\n");
				break;
			default:
				printf("[Unused Segement]\n");
				break;
		}
	}

	/* Find the (hopefully two) string tables */
	printf("\033[1mString Tables\033[0m\n");
	uint32_t i = 0;
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));
		if (shdr->sh_type != SHT_STRTAB) continue;
		string_table[i] = (char *)((uintptr_t)binary_buf + shdr->sh_offset);
		printf("Found a string table at 0x%x\n", shdr->sh_offset);
		++i;
		if (i == 5) break;
	}

	/* Read the section headers */
	printf("\033[1mSection Headers\033[0m\n");
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));

		printf("[%d] %s\n", shdr->sh_type, (char *)((uintptr_t)string_table[0] + shdr->sh_name));
		printf("Section starts at 0x%x and is 0x%x bytes long.\n", shdr->sh_offset, shdr->sh_size);
		if (shdr->sh_addr) {
			printf("It [0x%x] should be loaded at 0x%x.\n", ((uintptr_t)header + shdr->sh_offset), shdr->sh_addr);
			memcpy((void *)shdr->sh_addr,(void *)((uintptr_t)header + shdr->sh_offset), shdr->sh_size);
		}
	}
	printf("Done.\n");

	uintptr_t location = SOURCE;
	uintptr_t argc_    = (uintptr_t)&argc;
	__asm__ __volatile__ (
			"push $0\n"
			"push $0\n"
			"push %2\n"
			"push %1\n"
			"push $0\n"
			"call *%0\n"
			: : "m"(location), "m"(argc_), "m"(argv));

	return 0;
}

/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
