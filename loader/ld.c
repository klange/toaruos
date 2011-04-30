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
		return 2;
	}
	sbrk(DESTINATION + (END - SOURCE) - SOURCE);

	argc = argc_;
	argv = argv_;

	memcpy((void *)DESTINATION, (void *)SOURCE, END - SOURCE);
	uintptr_t location = ((uintptr_t)&_main - SOURCE + DESTINATION);
	__asm__ __volatile__ ("jmp *%0" : : "m"(location));

	return -1;
}

int _main() {
here:

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
		return 1;
	}

	/* Read the section headers */
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));

		if (shdr->sh_addr) {
			memcpy((void *)shdr->sh_addr,(void *)((uintptr_t)header + shdr->sh_offset), shdr->sh_size);
		}
	}

	uintptr_t location = header->e_entry;
	uintptr_t argc_    = (uintptr_t)&argc;
	__asm__ __volatile__ (
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
