/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 Kevin Lange
 *
 * ELF Dynamic Linker/Loader
 *
 * Loads ELF executables and links them at runtime to their
 * shared library dependencies.
 *
 * As of writing, this is a simplistic and not-fully-compliant
 * implementation of ELF dynamic linking. It suffers from a number
 * of issues, including not actually sharing libraries (there
 * isn't a sufficient mechanism in the kernel at the moment for
 * doing that - we need something with copy-on-write, preferably
 * an mmap-file mechanism), as well as not handling symbol
 * resolution correctly.
 *
 * However, it's sufficient for our purposes, and works well enough
 * to load Python C modules.
 */
#include <stdlib.h>
#include <stdint.h>
#include <alloca.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysfunc.h>

#include <kernel/elf.h>

void * (*_malloc)(size_t size) = malloc;
void (*_free)(void * ptr) = free;

#undef malloc
#undef free
#define malloc ld_x_malloc
#define free ld_x_free

uintptr_t _malloc_minimum = 0;

static void * malloc(size_t size) {
	return _malloc(size);
}

static void free(void * ptr) {
	if ((uintptr_t)ptr < _malloc_minimum) return;
	_free(ptr);
}

/*
 * When the LD_DEBUG environment variable is set, TRACE_LD messages
 * will be printed to stderr
 */
#define TRACE_APP_NAME "ld.so"
#define TRACE_LD(...) do { if (__trace_ld) { TRACE(__VA_ARGS__); } } while (0)

static int __trace_ld = 0;

#include <toaru/trace.h>

/*
 * This libraries are included in source form to avoid having
 * to build separate objects for them and complicate linking,
 * since ld is specially built as a static object.
 */
#include "../lib/list.c"
#include "../lib/hashmap.c"

typedef int (*entry_point_t)(int, char *[], char**);

/* Global linking state */
static hashmap_t * dumb_symbol_table;
static hashmap_t * glob_dat;
static hashmap_t * objects_map;

/* Used for dlerror */
static char * last_error = NULL;

static int _target_is_suid = 0;

typedef struct elf_object {
	FILE * file;

	/* Full copy of the header. */
	Elf32_Header header;

	char * dyn_string_table;
	size_t dyn_string_table_size;

	Elf32_Sym * dyn_symbol_table;
	size_t dyn_symbol_table_size;

	Elf32_Dyn * dynamic;
	Elf32_Word * dyn_hash;

	void (*init)(void);
	void (**init_array)(void);
	size_t init_array_size;

	uintptr_t base;

	list_t * dependencies;

	int loaded;

} elf_t;

static elf_t * _main_obj = NULL;

/* Locate library for LD_LIBRARY PATH */
static char * find_lib(const char * file) {

	/* If it was an absolute path, there's no need to find it. */
	if (strchr(file, '/')) return strdup(file);

	/* Collect the environment variable. */
	char * path = _target_is_suid ? NULL : getenv("LD_LIBRARY_PATH");
	if (!path) {
		/* Not set - this is the default state. Should probably read from config file? */
		path = "/lib:/usr/lib";
	}

	/* Duplicate so we can tokenize without editing */
	char * xpath = strdup(path);
	char * p, * last;
	for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
		/* Go through each LD_LIBRARY_PATH entry */
		int r;
		struct stat stat_buf;

		/* Append the requested file to that path */
		char * exe = malloc(strlen(p) + strlen(file) + 2);
		*exe = '\0';
		strcat(exe, p);
		strcat(exe, "/");
		strcat(exe, file);

		/* See if it exists */
		r = stat(exe, &stat_buf);
		if (r != 0) {
			/* Nope. */
			free(exe);
			continue;
		}

		/* It exists, so this is what we want. */
		return exe;
	}
	free(xpath);

	/* No match found. */
	return NULL;
}

/* Open an object file */
static elf_t * open_object(const char * path) {

	/* If no path (eg. dlopen(NULL)), return the main object (the executable). */
	if (!path) {
		return _main_obj;
	}

	/* If we've already opened a file with this name, return it - don't load things twice. */
	if (hashmap_has(objects_map, (void*)path)) {
		elf_t * object = hashmap_get(objects_map, (void*)path);
		return object;
	}

	/* Locate the library */
	char * file = find_lib(path);
	if (!file) {
		last_error = "Could not find library.";
		return NULL;
	}

	/* Open the library. */
	FILE * f = fopen(file, "r");

	/* Free the expanded path, we don't need it anymore. */
	free(file);

	/* Failed to open? Unlikely, but could mean permissions problems. */
	if (!f) {
		last_error = "Could not open library.";
		return NULL;
	}

	/* Initialize a fresh object object. */
	elf_t * object = malloc(sizeof(elf_t));
	memset(object, 0, sizeof(elf_t));
	hashmap_set(objects_map, (void*)path, object);

	/* Really unlikely... */
	if (!object) {
		last_error = "Could not allocate space.";
		return NULL;
	}

	object->file = f;

	/* Read the header */
	size_t r = fread(&object->header, sizeof(Elf32_Header), 1, object->file);

	/* Header failed to read? */
	if (!r) {
		last_error = "Failed to read object header.";
		free(object);
		return NULL;
	}

	/* Is this actually an ELF object? */
	if (object->header.e_ident[0] != ELFMAG0 ||
	    object->header.e_ident[1] != ELFMAG1 ||
	    object->header.e_ident[2] != ELFMAG2 ||
	    object->header.e_ident[3] != ELFMAG3) {

		last_error = "Not an ELF object.";
		free(object);
		return NULL;
	}

	/* Prepare a list for tracking dependencies. */
	object->dependencies = list_create();

	return object;
}

/* Calculate the size of an object file by examining its phdrs */
static size_t object_calculate_size(elf_t * object) {

	uintptr_t base_addr = 0xFFFFFFFF;
	uintptr_t end_addr  = 0x0;
	size_t headers = 0;
	while (headers < object->header.e_phnum) {
		Elf32_Phdr phdr;

		/* Read the phdr */
		fseek(object->file, object->header.e_phoff + object->header.e_phentsize * headers, SEEK_SET);
		fread(&phdr, object->header.e_phentsize, 1, object->file);

		switch (phdr.p_type) {
			case PT_LOAD:
				{
					/* If this loads lower than our current base... */
					if (phdr.p_vaddr < base_addr) {
						base_addr = phdr.p_vaddr;
					}

					/* Or higher than our current end address... */
					if (phdr.p_memsz + phdr.p_vaddr > end_addr) {
						end_addr = phdr.p_memsz + phdr.p_vaddr;
					}
				}
				break;
			/* TODO: Do we care about other PHDR types here? */
			default:
				break;
		}

		headers++;
	}

	/* If base_addr is still -1, then no valid phdrs were found, and the object has no loaded size. */
	if (base_addr == 0xFFFFFFFF) return 0;
	return end_addr - base_addr;
}

/* Load an object into memory */
static uintptr_t object_load(elf_t * object, uintptr_t base) {

	uintptr_t end_addr = 0x0;

	object->base = base;

	size_t headers = 0;
	while (headers < object->header.e_phnum) {
		Elf32_Phdr phdr;

		/* Read the phdr */
		fseek(object->file, object->header.e_phoff + object->header.e_phentsize * headers, SEEK_SET);
		fread(&phdr, object->header.e_phentsize, 1, object->file);

		switch (phdr.p_type) {
			case PT_LOAD:
				{
					/* Request memory to load this PHDR into */
					char * args[] = {(char *)(base + phdr.p_vaddr), (char *)phdr.p_memsz};
					sysfunc(TOARU_SYS_FUNC_MMAP, args);

					/* Copy the code into memory */
					fseek(object->file, phdr.p_offset, SEEK_SET);
					fread((void *)(base + phdr.p_vaddr), phdr.p_filesz, 1, object->file);

					/* Zero the remaining area */
					size_t r = phdr.p_filesz;
					while (r < phdr.p_memsz) {
						*(char *)(phdr.p_vaddr + base + r) = 0;
						r++;
					}

					/* If this expands our end address, be sure to update it */
					if (end_addr < phdr.p_vaddr + base + phdr.p_memsz) {
						end_addr = phdr.p_vaddr + base + phdr.p_memsz;
					}
				}
				break;
			case PT_DYNAMIC:
				{
					/* Keep a reference to the dynamic section, which is actually loaded by a PT_LOAD normally. */
					object->dynamic = (Elf32_Dyn *)(base + phdr.p_vaddr);
				}
				break;
			default:
				break;
		}

		headers++;
	}

	return end_addr;
}

/* Perform cleanup after loading */
static int object_postload(elf_t * object) {

	/* If there is a dynamic table, parse it. */
	if (object->dynamic) {
		Elf32_Dyn * table;

		/* Locate string tables */
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
				case 12: /* DT_INIT - initialization function */
					object->init = (void (*)(void))(table->d_un.d_ptr + object->base);
					break;
				case 25: /* DT_INIT_ARRAY - array of constructors */
					object->init_array = (void (**)(void))(table->d_un.d_ptr + object->base);
					break;
				case 27: /* DT_INIT_ARRAYSZ - size of the table of constructors */
					object->init_array_size = table->d_un.d_val / sizeof(uintptr_t);
					break;
			}
			table++;
		}

		/*
		 * Read through dependencies
		 * We have to do this separately from the above to make sure
		 * we have the dynamic string tables loaded first, as they
		 * are needed for the dependency names.
		 */
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

/* Whether symbol addresses is needed for a relocation type */
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

/* Apply ELF relocations */
static int object_relocate(elf_t * object) {

	/* If there is a dynamic symbol table, load symbols */
	if (object->dyn_symbol_table) {
		Elf32_Sym * table = object->dyn_symbol_table;
		size_t i = 0;
		while (i < object->dyn_symbol_table_size) {
			char * symname = (char *)((uintptr_t)object->dyn_string_table + table->st_name);

			/* If we haven't added this symbol to our symbol table, do so now. */
			if (!hashmap_has(dumb_symbol_table, symname)) {
				if (table->st_shndx) {
					hashmap_set(dumb_symbol_table, symname, (void*)(table->st_value + object->base));
				}
			}

			table++;
			i++;
		}
	}

	/* Find relocation table */
	for (uintptr_t x = 0; x < object->header.e_shentsize * object->header.e_shnum; x += object->header.e_shentsize) {
		Elf32_Shdr shdr;
		/* Load section header */
		fseek(object->file, object->header.e_shoff + x, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);

		/* Relocation table found */
		if (shdr.sh_type == 9) {
			Elf32_Rel * table = (Elf32_Rel *)(shdr.sh_addr + object->base);
			while ((uintptr_t)table - ((uintptr_t)shdr.sh_addr + object->base) < shdr.sh_size) {
				unsigned int  symbol = ELF32_R_SYM(table->r_info);
				unsigned char type = ELF32_R_TYPE(table->r_info);
				Elf32_Sym * sym = &object->dyn_symbol_table[symbol];

				/* If we need symbol for this, get it. */
				char * symname = NULL;
				uintptr_t x = sym->st_value + object->base;
				if (need_symbol_for_type(type) || (type == 5)) {
					symname = (char *)((uintptr_t)object->dyn_string_table + sym->st_name);
					if (symname && hashmap_has(dumb_symbol_table, symname)) {
						x = (uintptr_t)hashmap_get(dumb_symbol_table, symname);
					} else {
						/* This isn't fatal, but do log a message if debugging is enabled. */
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

/* Copy relocations are special and need to be located before other relocations. */
static void object_find_copy_relocations(elf_t * object) {

	for (uintptr_t x = 0; x < object->header.e_shentsize * object->header.e_shnum; x += object->header.e_shentsize) {
		Elf32_Shdr shdr;
		fseek(object->file, object->header.e_shoff + x, SEEK_SET);
		fread(&shdr, object->header.e_shentsize, 1, object->file);

		/* Relocation table found */
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

/* Find a symbol in a specific object. */
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

/* Fully load an object. */
static void * do_actual_load(const char * filename, elf_t * lib, int flags) {
	(void)flags;

	if (!lib) {
		last_error = "could not open library (not found, or other failure)";
		TRACE_LD("could not open library");
		return NULL;
	}

	size_t lib_size = object_calculate_size(lib);

	/* Needs to be at least a page. */
	if (lib_size < 4096) {
		lib_size = 4096;
	}

	/*
	 * Allocate space to load the library
	 * This is where we should really be loading things into COW
	 * but we don't have the functionality available.
	 */
	uintptr_t load_addr = (uintptr_t)malloc(lib_size);
	object_load(lib, load_addr);

	/* Perform cleanup steps */
	object_postload(lib);

	/* Ensure dependencies are available */
	node_t * item;
	while ((item = list_pop(lib->dependencies))) {

		elf_t * _lib = open_object(item->value);

		if (!_lib) {
			/* Missing dependencies are fatal to this process, but
			 * not to the entire application. */
			free((void *)load_addr);
			last_error = "Failed to load a dependency.";
			lib->loaded = 0;
			TRACE_LD("Failed to load object: %s", item->value);
			return NULL;
		}

		if (!_lib->loaded) {
			do_actual_load(item->value, _lib, 0);
			TRACE_LD("Loaded %s at 0x%x", item->value, lib->base);
		}

	}

	/* Perform relocations */
	TRACE_LD("Relocating %s", filename);
	object_relocate(lib);

	/* We're done with the file. */
	fclose(lib->file);

	/* If there was an init_array, call everything in it */
	if (lib->init_array) {
		for (size_t i = 0; i < lib->init_array_size; i++) {
			TRACE_LD(" 0x%x()", lib->init_array[i]);
			lib->init_array[i]();
		}
	}

	/* If the library has an init function, call that last. */
	if (lib->init) {
		lib->init();
	}

	lib->loaded = 1;

	/* And return an object for the loaded library */
	return (void *)lib;
}

static uintptr_t end_addr = 0;
/**
 * Half loads an object using the dumb allocator and performs dependency
 * resolution. This is a separate process from do_actual_load and dlopen_ld
 * to avoid problems with malloc and library functions while loading.
 * Preloaded objects will be fully loaded everything is relocated.
 */
static elf_t * preload(hashmap_t * libs, list_t * load_libs, char * lib_name) {
	/* Find and open the library */
	elf_t * lib = open_object(lib_name);

	if (!lib) {
		fprintf(stderr, "Failed to load dependency '%s'.\n", lib_name);
		return NULL;
	}

	/* Skip already loaded libraries */
	if (lib->loaded) return lib;

	/* Mark this library available */
	hashmap_set(libs, lib_name, lib);

	TRACE_LD("Loading %s at 0x%x", lib_name, end_addr);

	/* Adjust dumb allocator */
	while (end_addr & 0xFFF) {
		end_addr++;
	}

	/* Load PHDRs */
	end_addr = object_load(lib, end_addr);

	/* Extract information */
	object_postload(lib);

	/* Mark loaded */
	lib->loaded = 1;

	/* Verify dependencies are loaded before we relocate */
	foreach(node, lib->dependencies) {
		if (!hashmap_has(libs, node->value)) {
			TRACE_LD("Need unloaded dependency %s", node->value);
			preload(libs, load_libs, node->value);
		}
	}

	/* Add this to the (forward scan) list of libraries to finish loading */
	list_insert(load_libs, lib);

	return lib;
}

/* exposed dlopen() method */
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
	if (!ret) {
		/* Dependency load failure, remove us from hash */
		TRACE_LD("Dependency load failure");
		hashmap_remove(objects_map, (void*)filename);
	}

	TRACE_LD("Loaded %s at 0x%x", filename, lib->base);
	return ret;
}

/* exposed dlclose() method - XXX not fully implemented */
static int dlclose_ld(elf_t * lib) {
	/* TODO close dependencies? Make sure nothing references this. */
	free((void *)lib->base);
	return 0;
}

/* exposed dlerror() method */
static char * dlerror_ld(void) {
	char * this_error = last_error;
	last_error = NULL;
	return this_error;
}

/* Specially used by libc */
static void * _argv_value = NULL;
static char * argv_value(void) {
	return _argv_value;
}

/* Exported methods (dlfcn) */
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

	if (argc < 2) {
		fprintf(stderr,
				"ld.so - dynamic binary loader\n"
				"\n"
				"usage: %s [-e] [EXECUTABLE PATH]\n"
				"\n"
				" -e     \033[3mAdjust argument offset\033[0m\n"
				"\n", argv[0]);
		return -1;
	}

	char * file = argv[1];
	size_t arg_offset = 1;

	if (!strcmp(argv[1], "-e")) {
		arg_offset = 3;
		file = argv[2];
	}

	_argv_value = argv+arg_offset;

	/* Enable tracing if requested */
	char * trace_ld_env = getenv("LD_DEBUG");
	if ((trace_ld_env && (!strcmp(trace_ld_env,"1") || !strcmp(trace_ld_env,"yes")))) {
		__trace_ld = 1;
	}

	/* Initialize hashmaps for symbols, GLOB_DATs, and objects */
	dumb_symbol_table = hashmap_create(10);
	glob_dat = hashmap_create(10);
	objects_map = hashmap_create(10);

	/* Setup symbols for built-in exports */
	ld_exports_t * ex = ld_builtin_exports;
	while (ex->name) {
		hashmap_set(dumb_symbol_table, ex->name, ex->symbol);
		ex++;
	}

	/* Technically there's a potential time-of-use probably if we check like this but
	 * this is a toy linker for a toy OS so the fact that we even need to check suid
	 * bits at all is outrageous
	 */
	struct stat buf;
	if (stat(file, &buf)) {
		fprintf(stderr, "%s: target binary '%s' not available\n", argv[0], file);
	}

	/* Technically there's a way to know we're running suid, but let's check the actual file */
	if (buf.st_mode & S_ISUID) {
		_target_is_suid = 1;
	}

	/* Open the requested main object */
	elf_t * main_obj = open_object(file);
	_main_obj = main_obj;

	if (!main_obj) {
		fprintf(stderr, "%s: error: failed to open object '%s'.\n", argv[0], file);
		return 1;
	}

	/* Load the main object */
	end_addr = object_load(main_obj, 0x0);
	object_postload(main_obj);
	object_find_copy_relocations(main_obj);

	/* Load library dependencies */
	hashmap_t * libs = hashmap_create(10);

	while (end_addr & 0xFFF) {
		end_addr++;
	}

	/* Load dependent libraries, recursively. */
	TRACE_LD("Loading dependencies.");
	list_t * load_libs = list_create();
	node_t * item;
	while ((item = list_pop(main_obj->dependencies))) {
		char * lib_name = item->value;

		/* Skip libg.so which is a fake library that doesn't really exist.
		 * XXX: Only binaries should depend on this I think? */
		if (!strcmp(lib_name, "libg.so")) goto nope;

		/* Preload library */
		elf_t * lib = preload(libs, load_libs, lib_name);

		/* Failed to load */
		if (!lib) return 1;

nope:
		free(item);
	}

	list_t * ctor_libs = list_create();
	list_t * init_libs = list_create();
	while ((item = list_dequeue(load_libs))) {
		elf_t * lib = item->value;

		/* Complete relocation */
		object_relocate(lib);

		/* Close the underlying file */
		fclose(lib->file);

		/* Store constructors for later execution */
		if (lib->init_array) {
			list_insert(ctor_libs, lib);
		}
		if (lib->init) {
			list_insert(init_libs, lib);
		}

		free(item);
	}

	/* Relocate the main object */
	TRACE_LD("Relocating main object");
	object_relocate(main_obj);
	fclose(main_obj->file);
	TRACE_LD("Placing heap at end");
	while (end_addr & 0xFFF) {
		end_addr++;
	}

	/* Call constructors for loaded dependencies */
	char * ld_no_ctors = getenv("LD_DISABLE_CTORS");
	if (ld_no_ctors && (!strcmp(ld_no_ctors,"1") || !strcmp(ld_no_ctors,"yes"))) {
		TRACE_LD("skipping ctors because LD_DISABLE_CTORS was set");
	} else {
		foreach(node, ctor_libs) {
			elf_t * lib = node->value;
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

	/* If main object had constructors, call them. */
	if (main_obj->init_array) {
		for (size_t i = 0; i < main_obj->init_array_size; i++) {
			TRACE_LD(" 0x%x()", main_obj->init_array[i]);
			main_obj->init_array[i]();
		}
	}

	if (main_obj->init) {
		main_obj->init();
	}

	main_obj->loaded = 1;

	/* Move heap start (kind of like a weird sbrk) */
	{
		char * args[] = {(char*)end_addr};
		sysfunc(TOARU_SYS_FUNC_SETHEAP, args);
	}

	/* Set heap functions for later usage */
	if (hashmap_has(dumb_symbol_table, "malloc")) _malloc = hashmap_get(dumb_symbol_table, "malloc");
	if (hashmap_has(dumb_symbol_table, "free")) _free = hashmap_get(dumb_symbol_table, "free");
	_malloc_minimum = 0x40000000;

	/* Jump to the entry for the main object */
	TRACE_LD("Jumping to entry point");
	entry_point_t entry = (entry_point_t)main_obj->header.e_entry;
	entry(argc-arg_offset,argv+arg_offset,environ);

	return 0;
}
