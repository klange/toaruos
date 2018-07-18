/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2017 Kevin Lange
 */
#include <stdlib.h>
#include <stdint.h>
#include <alloca.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <kernel/elf.h>

extern char** environ;


#define TRACE_APP_NAME "ld.so"
#define TRACE_LD(...) do { if (__trace_ld) { TRACE(__VA_ARGS__); } } while (0)

static int __trace_ld = 0;

#include <toaru/trace.h>

#include "../lib/list.c"
#include "../lib/hashmap.c"

typedef int (*entry_point_t)(int, char *[], char**);

extern char end[];

static hashmap_t * dumb_symbol_table;
static hashmap_t * glob_dat;
static hashmap_t * objects_map;

static char * last_error = NULL;

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
	void (**ctors)(void);
	size_t ctors_size;
	void (**init_array)(void);
	size_t init_array_size;

	uintptr_t base;

	list_t * dependencies;

	int loaded;

} elf_t;

static elf_t * _main_obj = NULL;

static char * find_lib(const char * file) {

	if (strchr(file, '/')) return strdup(file);

	char * path = getenv("LD_LIBRARY_PATH");
	if (!path) {
		path = "/usr/lib:/lib:/opt/lib";
	}
	char * xpath = strdup(path);
	char * p, * last;
	for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
		int r;
		struct stat stat_buf;
		char * exe = malloc(strlen(p) + strlen(file) + 2);
		memcpy(exe, p, strlen(p) + 1);
		//strcpy(exe, p);
		strcat(exe, "/");
		strcat(exe, file);

		r = stat(exe, &stat_buf);
		if (r != 0) {
			free(exe);
			continue;
		}
		return exe;
	}
	free(xpath);


	return NULL;
}

static elf_t * open_object(const char * path) {

	if (!path) {
		_main_obj->loaded = 1;
		return _main_obj;
	}

	if (hashmap_has(objects_map, (void*)path)) {
		elf_t * object = hashmap_get(objects_map, (void*)path);
		object->loaded = 1;
		return object;
	}

	char * file = find_lib(path);
	if (!file) {
		last_error = "Could not find library.";
		return NULL;
	}

	FILE * f = fopen(file, "r");

	free(file);

	if (!f) {
		last_error = "Could not open library.";
		return NULL;
	}

	elf_t * object = calloc(1, sizeof(elf_t));
	hashmap_set(objects_map, (void*)path, object);

	if (!object) {
		last_error = "Could not allocate space.";
		return NULL;
	}

	object->file = f;

	size_t r = fread(&object->header, sizeof(Elf32_Header), 1, object->file);

	if (!r) {
		last_error = "Failed to read object header.";
		free(object);
		return NULL;
	}

	if (object->header.e_ident[0] != ELFMAG0 ||
	    object->header.e_ident[1] != ELFMAG1 ||
	    object->header.e_ident[2] != ELFMAG2 ||
	    object->header.e_ident[3] != ELFMAG3) {

		last_error = "Not an ELF object.";
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

	for (uintptr_t x = 0; x < object->header.e_shentsize * object->header.e_shnum; x += object->header.e_shentsize) {
		Elf32_Shdr shdr;
		fseek(object->file, object->header.e_shoff + x, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);

		if (!strcmp((char *)((uintptr_t)object->string_table + shdr.sh_name), ".ctors")) {
			object->ctors = (void *)(shdr.sh_addr + object->base);
			object->ctors_size = shdr.sh_size / sizeof(uintptr_t);
		}

		if (!strcmp((char *)((uintptr_t)object->string_table + shdr.sh_name), ".init_array")) {
			object->init_array = (void *)(shdr.sh_addr + object->base);
			object->init_array_size = shdr.sh_size / sizeof(uintptr_t);
		}
	}

	return 0;
}

static int need_symbol_for_type(unsigned char type) {
	switch(type) {
		case 1:
		case 2:
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
					//table->st_value = (uintptr_t)hashmap_get(dumb_symbol_table, symname);
				}
			}
			table++;
			i++;
		}
	}

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

				char * symname = NULL;
				uintptr_t x = sym->st_value + object->base;
				if (need_symbol_for_type(type) || (type == 5)) {
					symname = (char *)((uintptr_t)object->dyn_string_table + sym->st_name);
					if (symname && hashmap_has(dumb_symbol_table, symname)) {
						x = (uintptr_t)hashmap_get(dumb_symbol_table, symname);
					} else {
						TRACE_LD("Symbol not found: %s", symname);
						x = 0x0;
					}
				}

				/* Relocations, symbol lookups, etc. */
				switch (type) {
					case 6: /* GLOB_DAT */
						if (symname && hashmap_has(glob_dat, symname)) {
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
	if (!object->dyn_symbol_table) {
		last_error = "lib does not have a symbol table";
		return NULL;
	}

	Elf32_Sym * table = object->dyn_symbol_table;
	size_t i = 0;
	while (i < object->dyn_symbol_table_size) {
		if (!strcmp(symbol_name, (char *)((uintptr_t)object->dyn_string_table + table->st_name))) {
			return (void *)(table->st_value + object->base);
		}
		table++;
		i++;
	}

	last_error = "symbol not found in library";
	return NULL;
}

static void * do_actual_load(const char * filename, elf_t * lib, int flags) {
	(void)flags;

	if (!lib) {
		last_error = "could not open library (not found, or other failure)";
		return NULL;
	}

	size_t lib_size = object_calculate_size(lib);

	if (lib_size < 4096) {
		lib_size = 4096;
	}

	uintptr_t load_addr = (uintptr_t)malloc(lib_size);
	object_load(lib, load_addr);

	object_postload(lib);

	node_t * item;
	while ((item = list_pop(lib->dependencies))) {

		elf_t * lib = open_object(item->value);

		if (!lib) {
			free((void *)load_addr);
			last_error = "Failed to load a dependency.";
			return NULL;
		}

		if (!lib->loaded) {
			do_actual_load(item->value, lib, 0);
			TRACE_LD("Loaded %s at 0x%x", item->value, lib->base);
		}

	}

	TRACE_LD("Relocating %s", filename);
	object_relocate(lib);

	fclose(lib->file);

	if (lib->ctors) {
		for (size_t i = 0; i < lib->ctors_size; i++) {
			TRACE_LD(" 0x%x()", lib->ctors[i]);
			lib->ctors[i]();
		}
	}

	if (lib->init_array) {
		for (size_t i = 0; i < lib->init_array_size; i++) {
			TRACE_LD(" 0x%x()", lib->init_array[i]);
			lib->init_array[i]();
		}
	}

	if (lib->init) {
		lib->init();
	}

	return (void *)lib;

}

static void * dlopen_ld(const char * filename, int flags) {
	TRACE_LD("dlopen(%s,0x%x)", filename, flags);

	elf_t * lib = open_object(filename);

	if (!lib) {
		return NULL;
	}

	if (lib->loaded) {
		return lib;
	}

	void * ret = do_actual_load(filename, lib, flags);
	TRACE_LD("Loaded %s at 0x%x", filename, lib->base);
	return ret;
}

static int dlclose_ld(elf_t * lib) {
	/* TODO close dependencies? Make sure nothing references this. */
	free((void *)lib->base);
	return 0;
}

static char * dlerror_ld(void) {
	/* TODO actually do this */
	char * this_error = last_error;
	last_error = NULL;
	return this_error;
}

static void * _argv_value = NULL;
static char * argv_value(void) {
	return _argv_value;
}

typedef struct {
	char * name;
	void * symbol;
} ld_exports_t;
ld_exports_t ld_builtin_exports[] = {
	{"dlopen", dlopen_ld},
	{"dlsym", object_find_symbol},
	{"dlclose", dlclose_ld},
	{"dlerror", dlerror_ld},
	{"__get_argv", argv_value},
	{NULL, NULL},
};

int main(int argc, char * argv[]) {

	char * file = argv[1];
	size_t arg_offset = 1;


	if (!strcmp(argv[1], "-e")) {
		arg_offset = 3;
		file = argv[2];
	}

	_argv_value = argv+arg_offset;

	char * trace_ld_env = getenv("LD_DEBUG");
	if ((trace_ld_env && (!strcmp(trace_ld_env,"1") || !strcmp(trace_ld_env,"yes")))) {
		__trace_ld = 1;
	}

	dumb_symbol_table = hashmap_create(10);
	glob_dat = hashmap_create(10);
	objects_map = hashmap_create(10);

	ld_exports_t * ex = ld_builtin_exports;
	while (ex->name) {
		hashmap_set(dumb_symbol_table, ex->name, ex->symbol);
		ex++;
	}

	elf_t * main_obj = open_object(file);
	_main_obj = main_obj;

	if (!main_obj) {
		//fprintf(stderr, "%s: error: failed to open object '%s'.\n", argv[0], file);
		return 1;
	}

	uintptr_t end_addr = object_load(main_obj, 0x0);

	object_postload(main_obj);

	object_find_copy_relocations(main_obj);

	hashmap_t * libs = hashmap_create(10);

	while (end_addr & 0xFFF) {
		end_addr++;
	}

	list_t * ctor_libs = list_create();
	list_t * init_libs = list_create();

	TRACE_LD("Loading dependencies.");
	node_t * item;
	while ((item = list_pop(main_obj->dependencies))) {
		while (end_addr & 0xFFF) {
			end_addr++;
		}

		char * lib_name = item->value;
		if (!strcmp(lib_name, "libg.so")) goto nope;
		elf_t * lib = open_object(lib_name);
		if (!lib) {
			//fprintf(stderr, "Failed to load dependency '%s'.\n", lib_name);
			return 1;
		}
		hashmap_set(libs, lib_name, lib);

		TRACE_LD("Loading %s at 0x%x", lib_name, end_addr);
		end_addr = object_load(lib, end_addr);
		object_postload(lib);
		TRACE_LD("Relocating %s", lib_name);
		object_relocate(lib);

		fclose(lib->file);

		/* Execute constructors */
		if (lib->ctors || lib->init_array) {
			list_insert(ctor_libs, lib);
		}
		if (lib->init) {
			list_insert(init_libs, lib);
		}

nope:
		free(item);
	}

	TRACE_LD("Relocating main object");
	object_relocate(main_obj);
	TRACE_LD("Placing heap at end");
	while (end_addr & 0xFFF) {
		end_addr++;
	}

	char * ld_no_ctors = getenv("LD_DISABLE_CTORS");
	if (ld_no_ctors && (!strcmp(ld_no_ctors,"1") || !strcmp(ld_no_ctors,"yes"))) {
		TRACE_LD("skipping ctors because LD_DISABLE_CTORS was set");
	} else {
		foreach(node, ctor_libs) {
			elf_t * lib = node->value;
			if (lib->ctors) {
				TRACE_LD("Executing ctors...");
				for (size_t i = 0; i < lib->ctors_size; i++) {
					TRACE_LD(" 0x%x()", lib->ctors[i]);
					lib->ctors[i]();
				}
			}
			if (lib->init_array) {
				TRACE_LD("Executing init_array...");
				for (size_t i = 0; i < lib->init_array_size; i++) {
					TRACE_LD(" 0x%x()", lib->init_array[i]);
					lib->init_array[i]();
				}
			}
		}
	}

	foreach(node, init_libs) {
		elf_t * lib = node->value;
		lib->init();
	}

	if (main_obj->init_array) {
		for (size_t i = 0; i < main_obj->init_array_size; i++) {
			TRACE_LD(" 0x%x()", main_obj->init_array[i]);
			main_obj->init_array[i]();
		}
	}

	if (main_obj->init) {
		main_obj->init();
	}

	{
		char * args[] = {(char*)end_addr};
		syscall_system_function(9, args);
	}
	TRACE_LD("Jumping to entry point");

	entry_point_t entry = (entry_point_t)main_obj->header.e_entry;
	entry(argc-arg_offset,argv+arg_offset,environ);

	return 0;
}
