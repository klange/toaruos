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

#include "../userspace/lib/list.c"
#include "../userspace/lib/hashmap.c"

typedef int (*entry_point_t)(int, char *[], char**);

extern char end[];

struct elf_object {
	FILE * file;
	Elf32_Header header;
};


struct reloc {
	uintptr_t addr;
	uintptr_t replacement;
};

int return_butts() {
	return 1234;
}

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

	char * string_table = NULL;
	char * dyn_string_table = NULL;
	Elf32_Sym * dyn_symbol_table = NULL;

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

	{
		TRACE("Scanning for string table...");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		size_t i = 0;
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);
			if (shdr->sh_type == SHT_STRTAB) {
				if (i == main.header.e_shstrndx) {
					TRACE("Found section string table, copying somewhere safe.");
					string_table = malloc(shdr->sh_size);
					fseek(main.file, shdr->sh_offset, SEEK_SET);
					fread(string_table, shdr->sh_size, 1, main.file);
				}
			}
			i++;
		}
	}

	if (!string_table) {
		TRACE("no section string table?");
		return 1;
	}

	{
		TRACE("Scanning for dynamic string table...");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		size_t i = 0;
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);
			if (shdr->sh_type == SHT_STRTAB) {
				if (!strcmp((char *)((uintptr_t)string_table + shdr->sh_name), ".dynstr")) {
					TRACE("Found symbol string table. It is loaded at 0x%x, storing that for later.", shdr->sh_addr);
					dyn_string_table = (char *)shdr->sh_addr;
				}
			}
			i++;
		}
	}

	{
		TRACE("Scanning for dynamic symbol table...");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		size_t i = 0;
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);
			if (shdr->sh_type == 11) { /* TODO constant */
				if (!strcmp((char *)((uintptr_t)string_table + shdr->sh_name), ".dynsym")) {
					TRACE("Found dynamic symbol table. It is loaded at 0x%x, storing that for later.", shdr->sh_addr);
					Elf32_Sym * table = (Elf32_Sym *)(shdr->sh_addr);
					dyn_symbol_table = table;
					while ((uintptr_t)table - ((uintptr_t)shdr->sh_addr) < shdr->sh_size) {
						TRACE("%s: 0x%x [0x%x]", (char *)((uintptr_t)dyn_string_table + table->st_name), table->st_value, table->st_size);
						table++;
					}
				}
			}
			i++;
		}
	}

	if (!dyn_symbol_table) {
		TRACE("WARNING: No dynamic symbol table found?");
	}

	list_t * stuff_to_load = list_create();

	{
		TRACE("Scanning for dynamic section...");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		size_t i = 0;
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);
			if (shdr->sh_type == 6) { /* TODO constant */
				TRACE("Found dynamic section. Loaded at 0x%x.", shdr->sh_addr);
				Elf32_Dyn * table = (Elf32_Dyn *)(shdr->sh_addr);
				while ((uintptr_t)table - ((uintptr_t)shdr->sh_addr) < shdr->sh_size) {
					if (!table->d_tag) break;
					switch (table->d_tag) {
						case 1:
							TRACE("NEEDED - %s", dyn_string_table + table->d_un.d_val);
							list_insert(stuff_to_load, dyn_string_table + table->d_un.d_val);
							break;
						case 5:
							TRACE("STRTAB - 0x%x", table->d_un.d_ptr);
							break;
						case 6:
							TRACE("STRTAB - 0x%x", table->d_un.d_ptr);
							break;
						case 12:
							TRACE("INIT   - 0x%x", table->d_un.d_ptr);
							break;
						case 13:
							TRACE("INIT   - 0x%x", table->d_un.d_ptr);
							break;
						default:
							TRACE("%d - 0x%x", table->d_tag, table->d_un);
					}
					table++;
				}
			}
			i++;
		}
	}

	{
		TRACE("Need to load:");
		foreach(item, stuff_to_load) {
			TRACE(" - %s", item->value);
		}
	}

	{
		TRACE("Searching for REL sections to resolve.");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		size_t i = 0;
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);
			if (shdr->sh_type == 9) { /* TODO constant */
				Elf32_Rel * table = (Elf32_Rel *)(shdr->sh_addr);
				while ((uintptr_t)table - ((uintptr_t)shdr->sh_addr) < shdr->sh_size) {
					unsigned int  symbol = ELF32_R_SYM(table->r_info);
					unsigned char type = ELF32_R_TYPE(table->r_info);
					Elf32_Sym * sym = &dyn_symbol_table[symbol];
					TRACE("offset[0x%x] = %d %d (%s)", table->r_offset, symbol, type, dyn_string_table + sym->st_name);
					if (type == 6) {
						/* blob dat, memcpy size of symbol */
						memcpy((void *)table->r_offset, &sym->st_value, sizeof(uintptr_t));
					} else if (type == 7) {
						if (!strcmp(dyn_string_table + sym->st_name, "return_42") ) {
							/* lol */
							uintptr_t x = (uintptr_t)&return_butts;
							memcpy((void *)table->r_offset, &x, sizeof(x));
						}
					}
					table++;
				}
			}
			i++;
		}
	}

	{
		TRACE("Examining all section headers...");
		Elf32_Shdr * shdr = alloca(main.header.e_shentsize);
		for (uintptr_t x = 0 ; x < main.header.e_shentsize * main.header.e_shnum; x += main.header.e_shentsize) {
			fseek(main.file, main.header.e_shoff + x, SEEK_SET);
			fread(shdr, main.header.e_shentsize, 1, main.file);

			TRACE("[%d] %s at offset 0x%x of size 0x%x", shdr->sh_type, (char *)((uintptr_t)string_table + shdr->sh_name), shdr->sh_offset, shdr->sh_size);
		}
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
