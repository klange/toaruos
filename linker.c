#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <alloca.h>
#include <unistd.h>
#include <syscall.h>

#define TRACE_APP_NAME "ld.so"

#define TRACE_LD(...) do { if (__trace_ld) { TRACE(__VA_ARGS__); } } while (0)

static int __trace_ld = 0;

#include "../kernel/include/elf.h"
#include "../userspace/lib/trace.h"

#include "../userspace/lib/list.c"
#include "../userspace/lib/hashmap.c"

typedef int (*entry_point_t)(int, char *[], char**);

extern char end[];

static hashmap_t * dumb_symbol_table;
static hashmap_t * glob_dat;

typedef struct elf_object {
	FILE * file;

	/* Full copy of the header. */
	Elf32_Header header;

	/* Pointers to loaded stuff */
	char * string_table;

	char * dyn_string_table;
	size_t dyn_string_table_size;

	Elf32_Sym * dyn_symbol_table;
	size_t dyn_symbol_table_size;

	Elf32_Dyn * dynamic;
	Elf32_Word * dyn_hash;

	void (*init)(void);

	uintptr_t base;

	list_t * dependencies;

} elf_t;

static elf_t * open_object(const char * path) {

	FILE * f = fopen(path, "r");

	if (!f) {
		return NULL;
	}

	elf_t * object = calloc(1, sizeof(elf_t));

	if (!object) {
		return NULL;
	}

	object->file = f;

	size_t r = fread(&object->header, sizeof(Elf32_Header), 1, object->file);

	if (!r) {
		free(object);
		return NULL;
	}

	if (object->header.e_ident[0] != ELFMAG0 ||
	    object->header.e_ident[1] != ELFMAG1 ||
	    object->header.e_ident[2] != ELFMAG2 ||
	    object->header.e_ident[3] != ELFMAG3) {

		free(object);
		return NULL;
	}

	object->dependencies = list_create();

	return object;
}

static size_t object_calculate_size(elf_t * object) {

	uintptr_t base_addr = 0xFFFFFFFF;
	uintptr_t end_addr  = 0x0;

	{
		size_t headers = 0;
		while (headers < object->header.e_phnum) {
			Elf32_Phdr phdr;

			fseek(object->file, object->header.e_phoff + object->header.e_phentsize * headers, SEEK_SET);
			fread(&phdr, object->header.e_phentsize, 1, object->file);

			switch (phdr.p_type) {
				case PT_LOAD:
					{
						if (phdr.p_vaddr < base_addr) {
							base_addr = phdr.p_vaddr;
						}
						if (phdr.p_memsz + phdr.p_vaddr > end_addr) {
							end_addr = phdr.p_memsz + phdr.p_vaddr;
						}
					}
					break;
				default:
					break;
			}

			headers++;
		}
	}

	if (base_addr == 0xFFFFFFFF) return 0;
	return end_addr - base_addr;
}

static uintptr_t object_load(elf_t * object, uintptr_t base) {

	uintptr_t end_addr = 0x0;

	object->base = base;

	/* Load object */
	{
		size_t headers = 0;
		while (headers < object->header.e_phnum) {
			Elf32_Phdr phdr;

			fseek(object->file, object->header.e_phoff + object->header.e_phentsize * headers, SEEK_SET);
			fread(&phdr, object->header.e_phentsize, 1, object->file);

			switch (phdr.p_type) {
				case PT_LOAD:
					{
						char * args[] = {(char *)(base + phdr.p_vaddr), (char *)phdr.p_memsz};
						syscall_system_function(10, args);
						fseek(object->file, phdr.p_offset, SEEK_SET);
						fread((void *)(base + phdr.p_vaddr), phdr.p_filesz, 1, object->file);
						size_t r = phdr.p_filesz;
						while (r < phdr.p_memsz) {
							*(char *)(phdr.p_vaddr + base + r) = 0;
							r++;
						}

						if (end_addr < phdr.p_vaddr + base + phdr.p_memsz) {
							end_addr = phdr.p_vaddr + base + phdr.p_memsz;
						}
					}
					break;
				case PT_DYNAMIC:
					{
						object->dynamic = (Elf32_Dyn *)(base + phdr.p_vaddr);
					}
					break;
				default:
					break;
			}

			headers++;
		}
	}

	return end_addr;
}

static int object_postload(elf_t * object) {

	/* Load section string table */
	{
		Elf32_Shdr shdr;
		fseek(object->file, object->header.e_shoff + object->header.e_shentsize * object->header.e_shstrndx, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);
		object->string_table = malloc(shdr.sh_size);
		fseek(object->file, shdr.sh_offset, SEEK_SET);
		fread(object->string_table, shdr.sh_size, 1, object->file);
	}

	if (object->dynamic) {
		Elf32_Dyn * table;

		/* Locate string table */
		table = object->dynamic;
		while (table->d_tag) {
			switch (table->d_tag) {
				case 4:
					object->dyn_hash = (Elf32_Word *)(object->base + table->d_un.d_ptr);
					object->dyn_symbol_table_size = object->dyn_hash[1];
					break;
				case 5: /* Dynamic String Table */
					object->dyn_string_table = (char *)(object->base + table->d_un.d_ptr);
					break;
				case 6: /* Dynamic Symbol Table */
					object->dyn_symbol_table = (Elf32_Sym *)(object->base + table->d_un.d_ptr);
					break;
				case 10: /* Size of string table */
					object->dyn_string_table_size = table->d_un.d_val;
					break;
				case 12:
					object->init = (void (*)(void))(table->d_un.d_ptr + object->base);
					break;
			}
			table++;
		}

		table = object->dynamic;
		while (table->d_tag) {
			switch (table->d_tag) {
				case 1:
					list_insert(object->dependencies, object->dyn_string_table + table->d_un.d_val);
					break;
			}
			table++;
		}
	}

	return 0;
}

static int need_symbol_for_type(unsigned char type) {
	switch(type) {
		case 1:
		case 5:
		case 6:
		case 7:
			return 1;
		default:
			return 0;
	}
}


static int object_relocate(elf_t * object) {
	if (object->dyn_symbol_table) {
		Elf32_Sym * table = object->dyn_symbol_table;
		size_t i = 0;
		while (i < object->dyn_symbol_table_size) {
			char * symname = (char *)((uintptr_t)object->dyn_string_table + table->st_name);
			if (!hashmap_has(dumb_symbol_table, symname)) {
				if (table->st_shndx) {
					hashmap_set(dumb_symbol_table, symname, (void*)(table->st_value + object->base));
				}
			} else {
				if (table->st_shndx) {
					table->st_value = (uintptr_t)hashmap_get(dumb_symbol_table, symname);
				}
			}
			table++;
			i++;
		}
	}

	size_t i = 0;
	for (uintptr_t x = 0; x < object->header.e_shentsize * object->header.e_shnum; x += object->header.e_shentsize) {
		Elf32_Shdr shdr;
		fseek(object->file, object->header.e_shoff + x, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);

		if (shdr.sh_type == 9) {
			Elf32_Rel * table = (Elf32_Rel *)(shdr.sh_addr + object->base);
			while ((uintptr_t)table - ((uintptr_t)shdr.sh_addr + object->base) < shdr.sh_size) {
				unsigned int  symbol = ELF32_R_SYM(table->r_info);
				unsigned char type = ELF32_R_TYPE(table->r_info);
				Elf32_Sym * sym = &object->dyn_symbol_table[symbol];

				char * symname;
				uintptr_t x = sym->st_value + object->base;
				if ((sym->st_shndx == 0) && need_symbol_for_type(type) || (type == 5)) {
					symname = (char *)((uintptr_t)object->dyn_string_table + sym->st_name);
					if (hashmap_has(dumb_symbol_table, symname)) {
						x = (uintptr_t)hashmap_get(dumb_symbol_table, symname);
					} else {
						fprintf(stderr, "Symbol not found: %s\n", symname);
						x = 0x0;
					}
				}

				/* Relocations, symbol lookups, etc. */
				switch (type) {
					case 6: /* GLOB_DAT */
						if (hashmap_has(glob_dat, symname)) {
							x = (uintptr_t)hashmap_get(glob_dat, symname);
						}
					case 7: /* JUMP_SLOT */
						memcpy((void *)(table->r_offset + object->base), &x, sizeof(uintptr_t));
						break;
					case 1: /* 32 */
						x += *((ssize_t *)(table->r_offset + object->base));
						memcpy((void *)(table->r_offset + object->base), &x, sizeof(uintptr_t));
						break;
					case 2: /* PC32 */
						x += *((ssize_t *)(table->r_offset + object->base));
						x -= (table->r_offset + object->base);
						memcpy((void *)(table->r_offset + object->base), &x, sizeof(uintptr_t));
						break;
					case 8: /* RELATIVE */
						x = object->base;
						x += *((ssize_t *)(table->r_offset + object->base));
						memcpy((void *)(table->r_offset + object->base), &x, sizeof(uintptr_t));
						break;
					case 5: /* COPY */
						memcpy((void *)(table->r_offset + object->base), (void *)x, sym->st_size);
						break;
					default:
						TRACE_LD("Unknown relocation type: %d", type);
				}

				table++;
			}
		}
	}

	return 0;
}

static void object_find_copy_relocations(elf_t * object) {
	size_t i = 0;
	for (uintptr_t x = 0; x < object->header.e_shentsize * object->header.e_shnum; x += object->header.e_shentsize) {
		Elf32_Shdr shdr;
		fseek(object->file, object->header.e_shoff + x, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);

		if (shdr.sh_type == 9) {
			Elf32_Rel * table = (Elf32_Rel *)(shdr.sh_addr + object->base);
			while ((uintptr_t)table - ((uintptr_t)shdr.sh_addr + object->base) < shdr.sh_size) {
				unsigned char type = ELF32_R_TYPE(table->r_info);
				if (type == 5) {
					unsigned int  symbol = ELF32_R_SYM(table->r_info);
					Elf32_Sym * sym = &object->dyn_symbol_table[symbol];
					char * symname = (char *)((uintptr_t)object->dyn_string_table + sym->st_name);
					hashmap_set(glob_dat, symname, (void *)table->r_offset);
				}
				table++;
			}
		}
	}

}

static void * object_find_symbol(elf_t * object, const char * symbol_name) {
	if (!object->dyn_symbol_table) return NULL;

	Elf32_Sym * table = object->dyn_symbol_table;
	size_t i = 0;
	while (i < object->dyn_symbol_table_size) {
		if (!strcmp(symbol_name, (char *)((uintptr_t)object->dyn_string_table + table->st_name))) {
			return (void *)(table->st_value + object->base);
		}
		table++;
		i++;
	}

	return NULL;
}


static struct {
	const char * name;
	void * symbol;
} ld_builtin_exports[] = {
	{"_dl_open_object", open_object},
	{NULL, NULL}
};

int main(int argc, char * argv[]) {

	char * trace_ld_env = getenv("LD_DEBUG");
	if (trace_ld_env && (!strcmp(trace_ld_env,"1") || !strcmp(trace_ld_env,"yes"))) {
		__trace_ld = 1;
	}

	dumb_symbol_table = hashmap_create(10);
	glob_dat = hashmap_create(10);

	elf_t * main_obj = open_object(argv[1]);

	if (!main_obj) {
		fprintf(stderr, "%s: error: failed to open object '%s'.\n", argv[0], argv[1]);
		return 1;
	}

	size_t main_size = object_calculate_size(main_obj);
	uintptr_t end_addr = object_load(main_obj, 0x0);
	object_postload(main_obj);

	object_find_copy_relocations(main_obj);

	hashmap_t * libs = hashmap_create(10);

	TRACE_LD("Loading dependencies.");
	node_t * item;
	while (item = list_pop(main_obj->dependencies)) {
		while (end_addr & 0xFFF) {
			end_addr++;
		}

		char * lib_name = item->value;
		if (!strcmp(lib_name, "libg.so")) goto nope;
		elf_t * lib = open_object(lib_name);
		if (!lib) {
			fprintf(stderr, "Failed to load dependency '%s'.\n", lib_name);
			return 1;
		}
		hashmap_set(libs, lib_name, lib);

		TRACE_LD("Loading %s at 0x%x", lib_name, end_addr);
		end_addr = object_load(lib, end_addr);
		object_postload(lib);
		TRACE_LD("Relocating %s", lib_name);
		object_relocate(lib);

		fclose(lib->file);

		/* Execute init */
		lib->init();

nope:
		free(item);
	}

	TRACE_LD("Relocating main object");
	object_relocate(main_obj);
	TRACE_LD("Placing heap at end");
	while (end_addr & 0xFFF) {
		end_addr++;
	}

	{
		char * args[] = {(char*)end_addr};
		syscall_system_function(9, args);
	}
	TRACE_LD("Jumping to entry point");

	entry_point_t entry = (entry_point_t)main_obj->header.e_entry;
	entry(argc-1,argv+1,environ);

	return 0;
}
