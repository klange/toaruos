/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * ToAruOS Miniature ELF Reader
 * (C) 2011 Kevin Lange
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* The Master ELF Header */
#include "../../kernel/include/elf.h"

/**
 * Show usage for the readelf application.
 * @param argc Argument count (unused)
 * @param argv Arguments to binary
 */
void usage(int argc, char ** argv) {
	/* Show usage */
	printf("%s [filename]\n", argv[0]);
	printf("\tDisplays information on ELF binaries such as section names,\n");
	printf("\tlocations, sizes, and loading positions in memory.\n");
	exit(1);
}

/**
 * Application entry point.
 * @returns 0 on sucess, 1 on failure
 */
int main(int argc, char ** argv) {
	/* Process arguments */
	if (argc < 2) usage(argc,argv);

	FILE * binary;           /**< File pointer for requested executable */
	size_t binary_size;      /**< Size of the file */
	char * binary_buf;       /**< Buffer to store the binary in memory */
	Elf32_Header * header;   /**< ELF header */
	char * string_table;     /**< The section header string table */
	char * sym_string_table; /**< The symbol string table */

	/* Open the requested binary */
	binary = fopen(argv[1], "r");

	/* Jump to the end so we can get the size */
	fseek(binary, 0, SEEK_END);
	binary_size = ftell(binary);
	fseek(binary, 0, SEEK_SET);

	/* Some sanity checks */
	if (binary_size < 4 || binary_size > 0xFFFFFFF) {
		printf("Oh no! I don't quite like the size of this binary.\n");
		return 1;
	}
	printf("Binary is %u bytes.\n", (unsigned int)binary_size);

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

	uint32_t i = 0;
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));
		if (shdr->sh_type == SHT_STRTAB) {
			if (i == header->e_shstrndx) {
				string_table = (char *)((uintptr_t)binary_buf + shdr->sh_offset);
				printf("Found the section string table at 0x%x\n", shdr->sh_offset);
			}
		}
		i++;
	}

	/* Find the (hopefully two) string tables */
	printf("\033[1mString Tables\033[0m\n");
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));
		if (shdr->sh_type == SHT_STRTAB) {
			if (!strcmp((char *)((uintptr_t)string_table + shdr->sh_name), ".strtab")) {
				sym_string_table = (char *)((uintptr_t)binary_buf + shdr->sh_offset);
				printf("Found the symbol string table at 0x%x\n", shdr->sh_offset);
			}
			printf("Displaying string table at 0x%x\n", shdr->sh_offset);
			char * _string_table = (char *)((uintptr_t)binary_buf + shdr->sh_offset);
			unsigned int j = 1;
			int k = 0;
			printf("%d\n", shdr->sh_size);
			while (j < shdr->sh_size) {
				int t = strlen((char *)((uintptr_t)_string_table + j));
				if (t) {
					printf("%d [%d] %s\n", k, j, (char *)((uintptr_t)_string_table + j));
					k++;
					j += t;
				} else {
					j += 1;
				}
			}
		}
	}

	/* Read the section headers */
	printf("\033[1mSection Headers\033[0m\n");
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));

		printf("[%d] %s\n", shdr->sh_type, (char *)((uintptr_t)string_table + shdr->sh_name));
		printf("Section starts at 0x%x and is 0x%x bytes long.\n", shdr->sh_offset, shdr->sh_size);
		if (shdr->sh_addr) {
			printf("It should be loaded at 0x%x.\n", shdr->sh_addr);
		}
	}

#if 1
	printf("\033[1mSymbol Tables\033[0m\n");
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));

		if (shdr->sh_type == SHT_SYMTAB) {
			printf("Found symbol table: %s\n", (char *)((uintptr_t)string_table + shdr->sh_name));

			Elf32_Sym * table = (Elf32_Sym *)((uintptr_t)binary_buf + (shdr->sh_offset));
			while ((uintptr_t)table - ((uintptr_t)binary_buf + shdr->sh_offset) < shdr->sh_size) {
				printf("%s: 0x%x [0x%x]\n", (char *)((uintptr_t)sym_string_table + table->st_name), table->st_value, table->st_size);
				table++;
			}
		}
	}
#endif

	return 0;
}

/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
