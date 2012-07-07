/*
 * ToAruOS Loader
 * (C) 2011 Kevin Lange
 *
 * This is a broken, experimental `ld` binary loader.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>

/* The Master ELF Header */
#include "../kernel/include/elf.h"
#include "lib/ldlib.h"

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
uintptr_t END  = 0x0;
size_t    SIZE = 0x0;

int _main();
int argc;
char ** argv;
uintptr_t epoint = 0;

/**
 * Application entry point.
 * @returns 0 on sucess, 1 on failure
 */
int main(int argc_, char ** argv_) {

	END = (uintptr_t)sbrk(0);
	SIZE = END - SOURCE;
	DESTINATION = syscall_shm_obtain("ld.loader-library", &SIZE);

	fprintf(stderr, "[ld] Created space for LD library in shmem chunk [%p] size=0x%x\n", DESTINATION, SIZE);
	fprintf(stderr, "[ld] Heap end is at 0x%x\n", END);

	argc = argc_;
	argv = argv_;

	memcpy((void *)DESTINATION, (void *)SOURCE, SIZE);
	uintptr_t location = ((uintptr_t)&_main - SOURCE + DESTINATION);
	__asm__ __volatile__ ("jmp *%0" : : "m"(location));

	return -1;
}

static void * _memset(void *b, int val, size_t count) {
	__asm__ __volatile__ ("cld; rep stosb" : "+c" (count), "+D" (b) : "a" (val) : "memory");
	return b;
}
static void * _memcpy(void * restrict dest, const void * restrict src, size_t count) {
	__asm__ __volatile__ ("cld; rep movsb" : "+c" (count), "+S" (src), "+D" (dest) :: "memory");
	return dest;
}
static int _strcmp(const char * a, const char * b) {
	uint32_t i = 0;
	while (1) {
		if (a[i] < b[i]) {
			return -1;
		} else if (a[i] > b[i]) {
			return 1;
		} else {
			if (a[i] == '\0') {
				return 0;
			}
			++i;
		}
	}
}

int _main() {
here:

	fprintf(stderr, "[ld] Successful jump to destination.\n");

	/* Process arguments */
	if (argc < 2) usage(argc,argv);

	FILE * binary;           /**< File pointer for requested executable */
	size_t binary_size;      /**< Size of the file */
	char * binary_buf;       /**< Buffer to store the binary in memory */
	Elf32_Header * header;   /**< ELF header */
	char * string_table;     /**< Room for some string tables */
	uintptr_t __init = 0;
	char binary_name[512];

	/* Open the requested binary */
	binary = fopen(argv[1], "r");

	if (!binary) {
		printf("Oh no! This is terrible!\n");
		return 1;
	}

	/* Hack because we don't have seek/tell */
	fseek(binary, 0, SEEK_END);
	binary_size = ftell(binary);
	fseek(binary, 0, SEEK_SET);

	fprintf(stderr, "[ld] Binary to load is 0x%x bytes.\n", binary_size);
	if (binary_size > SIZE) {
		fprintf(stderr, "[ld] Need to ask for %d more space.\n", binary_size - SIZE);
	}

	sprintf(binary_name, "ld.binary.%s.%d", argv[1], getpid());

	fprintf(stderr, "[ld] Using buffer name `%s`\n", binary_name);

	/* Read the binary into a buffer */
	binary_buf = (void *)syscall_shm_obtain(binary_name, &binary_size);
	fread((void *)binary_buf, binary_size, 1, binary);

	/* Let's start considering this guy an elf, 'eh? */
	header = (Elf32_Header *)binary_buf;

	/* Verify the magic */
	if (	header->e_ident[0] != ELFMAG0 ||
			header->e_ident[1] != ELFMAG1 ||
			header->e_ident[2] != ELFMAG2 ||
			header->e_ident[3] != ELFMAG3) {
		fprintf(stderr, "[ld] Failed to load binary: bad magic\n");
		return 1;
	}

	uint32_t i = 0;
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)header + (header->e_shoff + x));
		if (i == header->e_shstrndx) {
			string_table = (char *)((uintptr_t)header + shdr->sh_offset);
		}
		++i;
	}

	if (!string_table) {
		fprintf(stderr, "[ld] No string table?\n");
		return 1;
	}

	/* TODO: Run through dynamic dependencies and load them first
	 * (into more SHM-mapped sections, as those are our mmap equivalents) */

	/* TODO: Run through every segment and expand the heap to fit the binary. */

	/*
	 * XXX
	 * BEYOND THIS POINT, DO NOT ATTEMPT ANY LIBRARY CALLS
	 * Why, you ask? Well, as much as /we/ are position independent, other libraries totally aren't.
	 * That even includes lib/ldlib, which we only used to store some functions required to jump into
	 * ourselfs after migration.
	 * XXX
	 */

	/* Read the section headers */
	for (uint32_t x = 0; x < header->e_shentsize * header->e_shnum; x += header->e_shentsize) {
		if (header->e_shoff + x > binary_size) {
			return 1;
		}
		Elf32_Shdr * shdr = (Elf32_Shdr *)((uintptr_t)binary_buf + (header->e_shoff + x));

		if (shdr->sh_addr) {
			if (shdr->sh_type == SHT_NOBITS) {
				/* This is the .bss, zero it */
				_memset((void *)(shdr->sh_addr), 0x0, shdr->sh_size);
			} else {
				_memcpy((void *)(shdr->sh_addr), (void *)((uintptr_t)header + shdr->sh_offset), shdr->sh_size);
			}
			char * sh_name = (char *)((uintptr_t)string_table + shdr->sh_name);
			if (!_strcmp(sh_name,".init")) {
				__init = shdr->sh_addr;
			}
		}
	}

#if 0
	if (__init) {
		__asm__ __volatile__ (
				"call *%0\n"
				: : "m"(__init));
	}
#endif

	/* Alright, boys, let's do this. */

	epoint = header->e_entry;
	__asm__ __volatile__ (
			"pushl $0\n"
			"pushl %2\n"
			"pushl %1\n"
			"pushl $0xDECADE21\n"
			"jmp *%0\n"
			: : "m"(epoint), "r"(argc-1), "r"(argv+1));

	return 0;
}

/*
 * vim:noexpandtab
 * vim:tabstop=4
 * vim:shiftwidth=4
 */
