#pragma once

#include <_cheader.h>
#include <sys/types.h>
#include <kernel/elf.h>

_Begin_C_Header

typedef struct elf_object {
	int file_fd;

	/* Full copy of the header. */
	Elf64_Header header;
	char header_extra[300];

	size_t header_size;

	char * dyn_string_table;
	size_t dyn_string_table_size;

	Elf64_Sym * dyn_symbol_table;
	size_t dyn_symbol_table_size;

	Elf64_Dyn * dynamic;
	Elf64_Word * dyn_hash;

	void (*init)(void);
	void (**init_array)(void);
	size_t init_array_size;

	uintptr_t base;

	list_t * dependencies;

	int loaded;

} elf_t;

_End_C_Header
