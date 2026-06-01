/**
 * @file libc/dlfcn/dl.c
 * @brief ld.so entry point
 *
 * This is the ELF dynamic linker. It self-relocates, examines auxv
 * entries to figure out what to do, loads and relocates dependencies
 * and a main binary (if any), and so on.
 *
 * This dynamic linker is near full rewrite of the old one that used
 * to live at @ref linker/linker.c - this one does much more normal
 * symbol resolution (rather than relying on a single shared table
 * of symbols), exists as a shared object (it's actually just the
 * libc.so), and deal with binaries that the kernel has already
 * alongside it.
 *
 * The symbol resolution, which better than it was previously, is
 * still not completely correct. We don't handle loading libraries
 * with @c dlopen flags or properly handle the flags to @c dlsym.
 * The only supported relocation types are ones that have been seen
 * in our own collection of applications and libraries.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <va_list.h>
#include <kernel/elf.h>

#include <libc/syscall.h>
#include <sys/syscall.h>

#include <libc/dlfcn/internal.h>
#include <libc/stdio/stdio_internal.h>
#include <libc/pthread/internal.h>
#include <libc/internal.h>

#ifdef LD_EARLY_DEBUG
static DEFN_SYSCALL3(_open,SYS_OPEN,const char*,long,mode_t);
static int emergency_fd = 0;
#endif

#define unlikely(cond) __builtin_expect((cond), 0)

struct DlLib {
	const char   * name;      /* Loaded name. */
	Elf64_Header * ehdr;      /* Start of Elf header. */
	Elf64_Phdr   * phdr;      /* Start of program headers. */
	size_t       phnum;       /* Number of program headers. */
	Elf64_Dyn    * full_dyn;  /* Pointer to the actual DYNAMIC section. */
	Elf64_Sym    * syms;      /* Start of symbol table. */
	const char   * strings;   /* Start of string table. */
	Elf64_Word   * hash;      /* Pointer to SysV symbol hashmap. */
	struct DlLib * next;      /* Next app/library in chain. */
	uintptr_t    base;        /* Loaded base address. */
	uintptr_t    dyn[32];     /* Direct map of DT tags to values. */
	uintptr_t    tlsbase;     /* Static TLS offset.*/
	size_t       tlssize;     /* How much space is reserved for static TLS. */
	bool         relocated;   /* Whether this library has had relocations applied already. */
	bool         constructed; /* Whether this library's constructors have been called. */
	char *       exe_path;    /* Full path of a library resolved in @c find_lib. */

	struct DlLib **dependencies; /* Allocated array of dependencies. */
	size_t       depcount;
};

#ifdef __x86_64__
#define ARCH_TLS_OFFSET_INIT 0
#define TLS_DOWN
#else
#define ARCH_TLS_OFFSET_INIT 16
#undef TLS_DOWN
#endif
static size_t current_tls_offset = ARCH_TLS_OFFSET_INIT;     /* How much space we've reserved in static TLS. */

static struct DlLib *all_libraries = NULL; /* Head of library chain. Usually ends up as the main app. */
static struct DlLib *last_library = NULL;  /* Tail of library chain. */
static struct DlLib *__libc_ldso = NULL;   /* Our own DlLib object. */
static bool is_runtime = false;            /* Whether we've finished loading and are now in runtime mode. */
static bool target_is_suid = false;        /* Whether the auxv indicate a set-user-id or set-group-id binary ("secure mode"). */
static char ** __envp = NULL;              /* Environment for use with @c simple_getenv. */
static bool __trace_ld = false;            /* Whether LD_DEBUG was set. */
static char * __ld_error = NULL;           /* Last error for @c dlerror. */
static bool __ldso_reported = false;       /* Whether ldd has reported libc.so yet. */
static bool __libc_in_chain = false;       /* if the libc is in all_libraries linked list */

_hidden bool __is_ldd = false;             /* Whether we are operating as the 'ldd' utility. */
_hidden char * __ld_preload = NULL;        /* LD_PRELOAD value */

static DEFN_SYSCALL1(_exit,SYS_EXT,int);

/**
 * @brief Simple memcpy.
 *
 * Used before we've self-relocated, before we can make a
 * symbolic reference to the real memcpy.
 *
 * @param a Destination.
 * @param b Source.
 * @param sz Size to copy.
 * @returns Nothing.
 */
static void simple_memcpy(char *a, char * b, size_t sz) {
	while (sz) {
		*a++ = *b++;
		sz--;
	}
}

/**
 * @brief Simple getenv.
 *
 * Used for all internal LD_ variable checks.
 *
 * @param var Environment variable name to look up.
 * @returns Pointer to environment variable value or NULL if not set.
 */
static char * simple_getenv(const char * var) {
	char ** envp = __envp;
	size_t len = strlen(var);
	for (char * e; (e = *envp); envp++) if (!strncmp(*envp,var,len) && e[len] == '=') return &e[len+1];
	return NULL;
}

/**
 * @brief Obtain own base address.
 *
 * Magic. On some platforms, we need to do silly tricks to ensure
 * we're getting the right resolved PC-relative address, which is
 * weird but whatever. Otherwise, this is the address of the start
 * of our headers which ld helpfully makes a symbol for but only
 * if we ask for one.
 */
static uintptr_t load_addr(void) {
	uintptr_t out;
#if defined(__aarch64__)
	__asm__(
	"  adrp %0, __ehdr_start\n"
	"  add %0, %0, #:lo12:__ehdr_start\n"
	:"=r"(out));
#else
	extern char __ehdr_start[] __attribute__((weak, visibility("hidden")));
	out = (uintptr_t)&__ehdr_start;
#endif
	return out;
}

/**
 * @brief Perform local relocations.
 *
 * This is the self-relocating part of ld.so/libc.so.
 * Does not perform any symbolic lookup; all symbolis resolved
 * here must be local.
 *
 * @param rel        Pointer to RELA table to process.
 * @param size       Size of table.
 * @param base       Base address to relocate to.
 * @param sym_table  Symbol table to use for index lookups.
 */
static void simple_relocs(uintptr_t rel, uintptr_t size, uintptr_t base, Elf64_Sym * sym_table) {
	Elf64_Rela * table = (Elf64_Rela *)rel;

	while ((uintptr_t)table - rel < size) {
		unsigned int symbol = ELF64_R_SYM(table->r_info);
		unsigned int type = ELF64_R_TYPE(table->r_info);
		Elf64_Sym * sym = &sym_table[symbol];
		uintptr_t x = base + sym->st_value;

		/* Relocations, symbol lookups, etc. */
		switch (type) {
#if defined(__aarch64__)
			case R_AARCH64_COPY:
				simple_memcpy((char *)(table->r_offset + base), (char *)x, sym->st_size);
				break;
			case R_AARCH64_RELATIVE:
				x = base;
				/* fallthrough */
			case R_AARCH64_ABS64:
				x += table->r_addend;
				/* fallthrough */
			case R_AARCH64_GLOB_DAT:
			case R_AARCH64_JUMP_SLOT:
				simple_memcpy((char *)(table->r_offset + base), (char *)&x, sizeof(uintptr_t));
				break;
#elif defined(__x86_64__)
			case R_X86_64_COPY: /* 5 */
				simple_memcpy((char *)(table->r_offset + base), (char *)x, sym->st_size);
				break;
			case R_X86_64_RELATIVE: /* 8*/
				x = base; // fallthrough
			case R_X86_64_64: /* 1 */
				x += table->r_addend; // fallthrough
			case R_X86_64_GLOB_DAT: /* 6 */
			case R_X86_64_JUMP_SLOT: /* 7 */
				simple_memcpy((char*)(table->r_offset + base), (char *)&x, sizeof(uintptr_t));
				break;
#else
# error "Unknown arch"
#endif
			default:
				//debug_print("unhandled reloc\n");
				syscall__exit(127);
				break;
		}

		table++;
	}
}

/**
 * @brief Calculate SysV symbol hash.
 *
 * This was copied from some Solaris docs.
 * Our binaries and libraries dont't currently have
 * GNU hashes, so we only support the SysV ones.
 *
 * @param _name Symbol name to hash.
 * @returns Hash value.
 */
static Elf64_Word elf_hash(const char *_name) {
	const unsigned char *name = (void*)_name;
	Elf64_Word h = 0, g;
	while (*name) {
		h = (h << 4) + *name++;
		if ((g = h & 0xf0000000)) h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/**
 * @brief Lookup a symbol by name in a given symbol table/hash table.
 *
 * Look up a symbol in a particular library by its hash/sym/str tables.
 *
 * @param table Hash table.
 * @param strtab String table.
 * @param symtab Symbol table.
 * @param name Symbol to look for.
 * @param h Hash of symbol name.
 * @returns Pointer into @p symtab or NULL if not found.
 */
static Elf64_Sym * elf_sym_lookup(Elf64_Word *table, const char *strtab, Elf64_Sym *symtab, const char *name, Elf64_Word h) {
	Elf64_Word nbuckets = table[0];
	for (size_t i = table[2 + h % nbuckets]; i; i = table[2 + nbuckets + i]) {
		if (!strcmp(strtab + symtab[i].st_name, name)) return symtab + i;
	}
	return NULL;
}

/**
 * @brief Debug print callback.
 *
 * Writes, character by character, debug messages, either
 * to standard error or the "emergency file descriptor".
 *
 * @param user Unused.
 * @param c    Character to write.
 * @returns 0 (implicit success)
 */
static int cb_dprintf(void * user, char c) {
	write(
#ifdef LD_EARLY_DEBUG
	emergency_fd,
#else
	STDERR_FILENO,
#endif
	&c, 1);
	return 0;
}

/**
 * @brief Debug print.
 *
 * Print formatted messages to standard error or another
 * emergency log file. This is used instead of the stdio
 * interface so as to not interface with any existing
 * buffering setup, and so that it can be used before
 * constructors are run.
 *
 * @param fmt Format string
 * @param ... Var args.
 * @returns Bytes written.
 */
static int dprintf(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = __printf_internal(cb_dprintf, NULL, fmt, args);
	va_end(args);
	return out;
}

/**
 * @brief TLS descriptor callback.
 *
 * Resolves TLS entries. All of our TLS entries
 * are static, so this always just returns the
 * value embedded at the calling address.
 */
__attribute__((unused))
static size_t __tlsdesc_static(size_t * a) {
	return a[1];
}

/**
 * @brief Is this a COPY relocation for this arch?
 */
static inline int is_copy(int type) {
#if defined(__aarch64__)
	return type == R_AARCH64_COPY;
#elif defined(__x86_64__)
	return type == R_X86_64_COPY;
#else
# error "Unknown arch"
#endif
}

/**
 * @brief Is this a TLS offset relocation for this arch?
 */
static inline int is_tlsoff(int type) {
#if defined(__aarch64__)
	return type == R_AARCH64_TLS_TPREL;
#elif defined(__x86_64__)
	return type == R_X86_64_TPOFF64;
#else
# error "Unknown arch"
#endif
}

/**
 * @brief Perform relocations for one library.
 *
 * Resolves symbols and applies all supported relocations
 * for a given library.
 *
 * @param lib Library to relocate.
 */
static void relocate(struct DlLib * lib) {
	uintptr_t * their_dyn = lib->dyn;
	uintptr_t   their_base = lib->base;

	size_t size = their_dyn[DT_RELASZ];
	uintptr_t reltable = their_base + their_dyn[DT_RELA];
	const char * strtab = lib->strings;
	Elf64_Sym  *symtab = lib->syms;


	for (int i = 0; i < 2; ++i) {
		Elf64_Rela *table  = (void*)reltable;
		while ((uintptr_t)table - reltable < size) {
			Elf64_Word symbol = ELF64_R_SYM(table->r_info);
			Elf64_Word type   = ELF64_R_TYPE(table->r_info);
			Elf64_Sym  *sym   = symbol ? &symtab[symbol] : NULL;

			uintptr_t x = 0;
			uintptr_t tlsx = 0;

			Elf64_Sym * resolved = NULL;
			struct DlLib * inlib = NULL;

			for (size_t j = 0; j < lib->phnum; ++j) {
				if (lib->phdr[j].p_type != PT_LOAD) continue;
				if (lib->phdr[j].p_vaddr > table->r_offset) continue;
				if (table->r_offset - lib->phdr[j].p_vaddr >= lib->phdr[j].p_memsz) continue;
				if (!(lib->phdr[j].p_flags & PF_W)) {
					if (__trace_ld) dprintf("ld.so: %s: Ignoring illegal relocation in read-only segment (%#zx)\n", lib->name, table->r_offset + lib->base);
					goto _continue;
				}
				break;
			}

			if (sym) {
				x = sym->st_shndx == (SHN_ABS ? 0 : their_base) + sym->st_value;

				if ((sym->st_info >> 4) == STB_LOCAL) {
					/* Nothing to look up for local symbol; use offset fetched above. */
				} else {
					const char *name = strtab + sym->st_name;
					Elf64_Word hash = elf_hash(name);

					struct DlLib * dep = (is_copy(type) ? lib->next : all_libraries);

					for (; dep; dep = dep->next) {
						Elf64_Sym *maybe = elf_sym_lookup(dep->hash, dep->strings, dep->syms, name, hash);
						if (maybe && maybe->st_shndx != SHN_UNDEF) {
							resolved = maybe;
							inlib = dep;
							break;
						}
					}

					if (resolved) {
						if (unlikely(__trace_ld)) {
							dprintf("ld.so: %s: Resolved symbol '%s' as %#zx\n",
								lib->name, name, (inlib->base + resolved->st_value));
						}
						x = inlib->base + resolved->st_value;
#ifdef TLS_DOWN
						tlsx = resolved->st_value - inlib->tlsbase;
#else
						tlsx = inlib->tlsbase + resolved->st_value;
#endif
					} else {
						if ((sym->st_info >> 4) != STB_WEAK) {
							dprintf("ld.so: could not resolve symbol '%s' in %s\n", name, lib->name);
						}
						goto _continue;
					}
				}
			}

			switch (type) {
#if defined(__aarch64__)
				case R_AARCH64_COPY:
					memcpy((void *)(table->r_offset + their_base), (void *)x, sym->st_size);
					break;
				case R_AARCH64_RELATIVE:
					x = their_base; // fallthrough
				case R_AARCH64_ABS64:
					x += table->r_addend; //fallthrough
				case R_AARCH64_GLOB_DAT:
				case R_AARCH64_JUMP_SLOT:
					memcpy((void*)(table->r_offset + their_base), &x, sizeof(uintptr_t));
					break;
				case R_AARCH64_TLS_TPREL:
					tlsx += table->r_addend;
					memcpy((void*)(table->r_offset + their_base), &tlsx, sizeof(uintptr_t));
					break;
				case R_AARCH64_TLSDESC:
					tlsx += table->r_addend;
					uintptr_t func = (uintptr_t)&__tlsdesc_static;
					memcpy((void *)(table->r_offset + their_base), &func, sizeof(uintptr_t));
					memcpy((void *)(table->r_offset + their_base + sizeof(uintptr_t)), &tlsx, sizeof(uintptr_t));
					break;
#elif defined(__x86_64__)
				case R_X86_64_COPY:
					memcpy((void *)(table->r_offset + their_base), (void *)x, sym->st_size);
					break;
				case R_X86_64_RELATIVE:
					x = their_base; // fallthrough
				case R_X86_64_64:
					x += table->r_addend; //fallthrough
				case R_X86_64_GLOB_DAT:
				case R_X86_64_JUMP_SLOT:
					memcpy((void*)(table->r_offset + their_base), &x, sizeof(uintptr_t));
					break;
				case R_X86_64_TPOFF64:
					tlsx += table->r_addend;
					memcpy((void*)(table->r_offset + their_base), &tlsx, sizeof(uintptr_t));
					break;
				case R_X86_64_DTPMOD64:
					x = 0;
					memcpy((void *)(table->r_offset + their_base), &x, sizeof(uintptr_t));
					break;
				case R_X86_64_DTPOFF64:
					tlsx += table->r_addend;
					memcpy((void *)(table->r_offset + their_base), &tlsx, sizeof(uintptr_t));
					break;
#else
# error "Unknown arch"
#endif
				default:
					dprintf("ld.so: unhandled relocation type %d\n", type);
					break;
			}

_continue:
			table++;
		}

		if (i == 1) break;
		reltable = their_base + their_dyn[DT_JMPREL];
		size  = their_dyn[DT_PLTRELSZ];
	}
}

static void setup_lib(struct DlLib * app, Elf64_Phdr *phdrs, size_t phnum);
static void do_preloads(void);
static void calculate_tls_size(struct DlLib * app);

/**
 * @brief Attempt to load (map) an opened library.
 *
 * Parses the headers for a library from an opened file and maps the library
 * into memory. Recursively attempts to do the same for any unloaded dependencies.
 * Does not perform relocations - that happens later.
 *
 * @param name Loaded name of library.
 * @param fd   File descriptor of opened library file.
 * @param parent (Unused, but should be the library that caused this one to be loaded.)
 * @param is_exec If this is an executable (from a direct call to ld.so with a command line.)
 * @returns DlLib object or NULL if loading failed.
 */
static struct DlLib * try_load(const char * name, int fd, struct DlLib * parent, int is_exec) {
	struct DlLib * lib = calloc(1, sizeof(struct DlLib));
	size_t avail = sizeof(struct Elf64_Header) + 300;
	Elf64_Header * lib_header = calloc(1, avail);
	ssize_t r = pread(fd, lib_header, avail, 0);

	/* Read failed or not big enough to be a valid ELF object. */
	if (r < 0 || (size_t)r < avail) goto _fail_dep;

	/* Check file type. Only a main executable specified on the commandline is allowed to be
	 * an ET_EXEC; everything else must be an ET_DYN. */
	if (lib_header->e_type != ET_DYN && (!is_exec || lib_header->e_type != ET_EXEC)) goto _fail_dep;
	if (lib_header->e_phoff + lib_header->e_phentsize > avail) {
		dprintf("ld.so: %s: need to load more phdrs, failing for now\n", name);
		goto _fail_dep;
	}

	uintptr_t load_addr = 0;

	if (!is_exec) {
		uintptr_t base_addr = (uintptr_t)-1;
		uintptr_t end_addr = 0x0;

		/* Calculate the maximum extents of the loadable segments. */
		for (size_t i = 0; i < lib_header->e_phnum; ++i) {
			Elf64_Phdr * phdr = (void*)((uintptr_t)lib_header + lib_header->e_phoff + lib_header->e_phentsize * i);
			if (phdr->p_type != PT_LOAD) continue;
			if ((phdr->p_vaddr & ~0xFFF) < base_addr) base_addr = (phdr->p_vaddr & ~0xFFF);
			if (phdr->p_vaddr + phdr->p_memsz > end_addr) end_addr = phdr->p_vaddr + phdr->p_memsz;
		}

		/* We use @c valloc so that we can potentially call @c free later; we'll override
		 * the whole mapping with calls to @c mmap along the way, but this does mean we're
		 * losing space on gaps... oh well. */
		load_addr = (uintptr_t)valloc(end_addr - base_addr);
	}

	if (unlikely(__trace_ld)) {
		dprintf("ld.so: %s: loading at %#zx\n", name, load_addr);
	}

	for (size_t i = 0; i < lib_header->e_phnum; ++i) {
		Elf64_Phdr * phdr = (void*)((uintptr_t)lib_header + lib_header->e_phoff + lib_header->e_phentsize * i);
		if (phdr->p_type != PT_LOAD) continue;

		if (unlikely(__trace_ld)) {
			dprintf("ld.so: %s: load vaddr=%#zx, size=%zu, offset=%zu, filesz=%zu\n",
				name, phdr->p_vaddr, phdr->p_memsz, phdr->p_offset, phdr->p_filesz);
		}

		/* Page align everything, aligning base addresses down, sizes up. */
		size_t    pageoffset = ((load_addr + phdr->p_vaddr) & 0xFFF);
		uintptr_t addr   = load_addr + phdr->p_vaddr - pageoffset;
		size_t    size   = phdr->p_filesz + pageoffset;
		off_t     offset = phdr->p_offset - pageoffset;
		size = (size + 0xFFF) & ~0xFFF;

		int prot = PROT_READ;
		if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
		if (phdr->p_flags & PF_X) prot |= PROT_EXEC;

		/* Map it. */
		char * mapped_to = mmap((void*)addr, size, prot, MAP_PRIVATE | MAP_FIXED, fd, offset);

		/* Only try to zero out extra space in the last page if we can write to the resulting segment. */
		if (phdr->p_flags & PF_W) {
			uintptr_t pad = (uintptr_t)mapped_to + pageoffset + phdr->p_filesz;
			if (pad & 0xFFF) {
				size_t fill = 0x1000 - (pad & 0xFFF);
				memset((void*)pad, 0, fill);
			}
		}

		/* Map any extra pages specified by memsz. Newly mapped pages will be zerod by the kernel, so
		 * we don't need to do that ourselves. */
		if (phdr->p_memsz > phdr->p_filesz) {
			uintptr_t start = (uintptr_t)mapped_to + pageoffset + phdr->p_filesz;
			uintptr_t end   = (uintptr_t)mapped_to + pageoffset + phdr->p_memsz;
			uintptr_t start_page = (start + 0xFFF) & ~(0xFFF);
			uintptr_t end_page   = (end + 0xFFF) & ~(0xFFF);
			if (end_page > start_page) {
				mmap((void*)start_page, end_page - start_page, prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
			}
		}
	}

	/* Once we've called @c mmap we can close the file. */
	close(fd);

	/* Set up the DlLib object for the new library. */
	lib->name = strdup(name);
	lib->ehdr = lib_header;
	lib->base = load_addr;

	Elf64_Phdr * phdrs = (void*)((uintptr_t)lib_header + lib_header->e_phoff);

	/* Link it into our list of libraries. */
	if (is_exec) {
		all_libraries = lib;
		last_library = lib;

		lib->next = NULL;
		lib->phdr = phdrs;
		lib->phnum = lib_header->e_phnum;

		calculate_tls_size(lib);
		do_preloads();
	} else {
		lib->next = NULL;
		last_library->next = lib;
		last_library = lib;
	}

	/* And then load any dependencies. */
	setup_lib(lib, phdrs, lib_header->e_phnum);

	return lib;

_fail_dep:
	dprintf("ld.so: failed to load dependency '%s'\n", name);
	free(lib_header);
	free(lib);
	close(fd);
	return NULL;
}

/**
 * @brief Report a library as ldd.
 *
 * Prints a line describing a loaded library's name, possibly
 * file path, and address.
 *
 * @param lib Library to report.
 */
static void ldd_report(struct DlLib * lib) {
	fprintf(stderr, "\t%s", lib->name);
	if (lib->exe_path) fprintf(stderr, " => %s", lib->exe_path);
	fprintf(stderr, " (%#zx)\n", lib->base);
}

/**
 * @brief Report a failure as ldd.
 *
 * Writes a line saying a library was not found.
 *
 * @param name Name of missing library.
 */
static void ldd_failure(const char *name) {
	fprintf(stderr, "\t%s => not found\n", name);
}

/**
 * @brief Resolve and load libraries by name.
 *
 * Looks for libraries, either from absolute paths or by
 * searching in @c LD_LIBRARY_PATH (or the fallback set
 * of @c /lib:/usr/lib if that isn't set or we are in
 * "secure mode"). When a suitable library is found, it
 * is then loaded.
 *
 * If @p name refers to a library that has already been loaded,
 * then the existing library object is returned and no further
 * loading happens.
 *
 * TODO This doesn't handle any of the fun rpath/origin things.
 *
 * @param name Name of library to look for.
 * @param parent (Unused, but should be library that caused this one to be searched.)
 * @returns DlLib object or NULL on failure.
 */
static struct DlLib * find_lib(const char * name, struct DlLib * parent) {
	if (!strcmp(name,"libc.so")) {
		if (unlikely(__is_ldd) && !__ldso_reported) {
			__ldso_reported = true;
			ldd_report(__libc_ldso);
		}
		if (!__libc_in_chain) {
			last_library->next = __libc_ldso;
			last_library = __libc_ldso;
			__libc_in_chain = true;
		}
		return __libc_ldso;
	}

	struct DlLib * ptr = all_libraries;
	while (ptr) {
		if (!strcmp(ptr->name, name)) return ptr;
		ptr = ptr->next;
	}

	if (strchr(name, '/')) {
		int fd = open(name, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			__ld_error = "failed to open file";
			return NULL;
		}

		struct DlLib * lib = try_load(name, fd, parent, 0);
		if (unlikely(__is_ldd) && lib) ldd_report(lib);
		return lib;
	}

	/* Did not find it, let's go load it. */
	char * path = "/lib:/usr/lib";

	if (!target_is_suid) {
		/* Only check @c LD_LIBRARY_PATH if we aren't in "secure mode". */
		char * p = simple_getenv("LD_LIBRARY_PATH");
		if (p) path = p;
	}

	char * xpath = strdup(path); /* Because strtok will modify it. */
	char *p, *last;
	for ((p = strtok_r(xpath, ":", &last)); p;
	      p = strtok_r(NULL, ":", &last)) {
		ssize_t r;
		struct stat sb;

		char * exe;
		asprintf(&exe, "%s/%s", p, name);

		r = stat(exe, &sb);
		if (r != 0) {
			free(exe);
			continue;
		}

		/* Open file. */
		int fd = open(exe, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			free(exe);
			continue;
		}

		/* Load library. */
		struct DlLib * maybe = try_load(name, fd, parent, 0);
		if (!maybe) {
			free(exe);
			continue;
		}

		/* Attach resolved path so we can report it. */
		maybe->exe_path = exe;
		if (unlikely(__is_ldd)) ldd_report(maybe);

		free(xpath);
		return maybe;
	}

	free(xpath);

	__ld_error = "no suitable library found";
	return NULL;
}

/**
 * @brief Calculate the size of TLS data.
 *
 * Locates the TLS segment for the library and sets up the
 * TLS offset appropriately, growing up or down depending
 * on the platform.
 *
 * @param lib Library to set up.
 */
static void calculate_tls_size(struct DlLib * lib) {
	if (lib->tlssize || lib->tlsbase) return;

	if (lib->phdr) {
		for (size_t i = 0; i < lib->phnum; ++i) {
			if (lib->phdr[i].p_type == PT_TLS) {

				/* TODO */
				if (lib->phdr[i].p_filesz != 0) fprintf(stderr, "ld.so: warning: initialized TLS segment not supported\n");

				lib->tlssize = lib->phdr[i].p_memsz;
#ifdef TLS_DOWN
				current_tls_offset += lib->tlssize;
				if (lib->phdr[i].p_align) {
					while (current_tls_offset & (lib->phdr[i].p_align -1)) current_tls_offset++;
				}
				lib->tlsbase = current_tls_offset;
#else
				if (lib->phdr[i].p_align) {
					while (current_tls_offset & (lib->phdr[i].p_align -1)) current_tls_offset++;
				}
				lib->tlsbase = current_tls_offset;
				current_tls_offset += lib->tlssize;
#endif
				return;
			}
		}
	}
}

/**
 * @brief Fill out a DlLib object and load dependencies.
 *
 * Caches values from the DYNAMIC section, examines
 * dependencies from @c DT_NEEDED entries, loads any
 * that haven't already been loaded, and calculates
 * the size of static TLS data for later relocation.
 *
 * @param app   DlLib to set up.
 * @param phdrs Source of program headers for this library/app.
 * @param phnum Number of program headers for this library/app.
 */
static void setup_lib(struct DlLib * app, Elf64_Phdr *phdrs, size_t phnum) {
	app->phdr  = phdrs;
	app->phnum = phnum;
	for (size_t i = 0; i < phnum; ++i) {
		if (phdrs[i].p_type == PT_DYNAMIC) {
			Elf64_Dyn *_dyn = (void*)(phdrs[i].p_vaddr + app->base);
			app->full_dyn = _dyn;
			while (_dyn->d_tag) {
				if (_dyn->d_tag < 32) app->dyn[_dyn->d_tag] = _dyn->d_un.d_val;
				_dyn++;
			}
			break;
		}
	}

	app->strings = (void*)(app->base + app->dyn[DT_STRTAB]);
	app->syms    = (void*)(app->base + app->dyn[DT_SYMTAB]);
	app->hash    = (void*)(app->base + app->dyn[DT_HASH]);

	/* Count deps */
	size_t count = 0;
	for (Elf64_Dyn *d = app->full_dyn; d->d_tag; d++) {
		if (d->d_tag != DT_NEEDED) continue;
		count++;
	}

	/* We don't use this yet, but it's critical for correct handling
	 * of some symbol resolution that we'll get around to eventually. */
	app->dependencies = calloc(count, sizeof(struct DlLib *));
	app->depcount = count;
	count = 0;

	for (Elf64_Dyn *d = app->full_dyn; d->d_tag; d++) {
		if (d->d_tag != DT_NEEDED) continue;
		const char * name = app->strings + d->d_un.d_val;
		struct DlLib * dep = find_lib(name, app);
		if (unlikely(__is_ldd) && !dep) ldd_failure(name);
		app->dependencies[count++] = dep;
	}

	calculate_tls_size(app);
}

/**
 * @brief Relocate a library and every library after it in the chain.
 *
 * Performs relocations on every library that hasn't already been
 * marked as relocated, starting from @p lib. Those libraries are
 * then marked as relocated so they won't be relocated again. In
 * some situations, a library (mostly us, the libc) may need to be
 * relocated multiple times, so @c relocate is used directly without
 * setting the @c relocated flag, allowing a future call to this
 * function to relocate it again.
 *
 * @param lib First library to relocate.
 */
static void relocate_stuff(struct DlLib *lib) {
	for (; lib; lib = lib->next) {
		if (lib->relocated) continue;
		relocate(lib);
		lib->relocated = true;
	}
}

/**
 * @brief Run constructors for a library.
 *
 * Marks the library as constructed and runs its constructors.
 *
 * @param lib Library to run constructors from.
 */
static void run_ctors(struct DlLib * lib) {
	lib->constructed = true;
	uintptr_t __init_array_start = lib->base + lib->dyn[DT_INIT_ARRAY];
	uintptr_t __init_array_end   = __init_array_start + lib->dyn[DT_INIT_ARRAYSZ];
	for (uintptr_t * constructor = (uintptr_t*)__init_array_start; constructor < (uintptr_t*)__init_array_end; ++constructor) {
		void (*constr)(void) = (void*)*constructor;
		constr();
	}
}

void * dlopen(const char * filename, int flags) {
	if (filename == NULL) return all_libraries;

	struct DlLib * res = find_lib(filename, NULL);

	if (res) {
		relocate_stuff(all_libraries);

		for (struct DlLib * libs = all_libraries; libs; libs = libs->next) {
			if (libs->constructed) continue;
			run_ctors(libs);
		}
	}

	return res;
}

/**
 * @brief Determine which library owns an address.
 *
 * Examines the loaded libraries and looks for one
 * with a PT_LOAD segment that contains this address.
 *
 * @param from Address to look for.
 * @returns Library for that address, or NULL if none.
 */
static struct DlLib * lib_at(uintptr_t from) {
	for (struct DlLib * me = all_libraries; me; me = me->next) {
		for (size_t i = 0; i < me->phnum; i++) {
			if (me->phdr[i].p_type != PT_LOAD) continue;
			if (from - me->base >= me->phdr[i].p_vaddr &&
				from - me->base - me->phdr[i].p_vaddr < me->phdr[i].p_memsz) {
				return me;
			}
		}
	}
	return NULL;
}

void * dlsym(void *_lib, const char * name) {
	struct DlLib * lib = _lib;
	Elf64_Word hash = elf_hash(name);

	if (lib == RTLD_NEXT) {
		uintptr_t from = (uintptr_t)__builtin_return_address(0);
		/* figure out who asked */
		lib = lib_at(from);
		if (lib) lib = lib->next;
	}

	if (lib == NULL) lib = all_libraries;

	for (struct DlLib * dep = lib; dep; dep = dep->next) {
		Elf64_Sym *maybe = elf_sym_lookup(dep->hash, dep->strings, dep->syms, name, hash);
		if (maybe && maybe->st_shndx != SHN_UNDEF) {
			return (void*)(dep->base + maybe->st_value);
		}
	}

	return NULL;
}

int __attribute__((weak)) dlclose(void * handle) {
	__ld_error = "dlclose() unimplemented";
	return -1;
}

char * dlerror(void) {
	return __ld_error;
}

/**
 * @brief Actually execute code.
 *
 * Finish relocations, run all necessary constructors, and finally
 * jump to the entry point of the main application.
 *
 * @param app    Main application (the one whose entry point we'll jump to).
 * @param argc   Argument count.
 * @param argv   Argument vector.
 * @param entryp Entry point address.
 * @returns Whatever the entry point returns, if it does.
 */
static int run_app(struct DlLib * app, int argc, char * argv[], uintptr_t entryp) {
	relocate_stuff(app->next);
	relocate_stuff(app);

	/* If a legacy app defined errno, ours will have been relocated to match;
	 * we set the address in the thread pointer early for the dynamic linker,
	 * but we need to re-adjust here. */
	pthread_self()->err_addr = &__errno;

	/* Must call this before other constructors to set up environ,
	 * stdio, etc. */
	__libc_init();

	for (struct DlLib * libs = app->next; libs; libs = libs->next) {
		if (libs->constructed) continue;
		run_ctors(libs);
	}
	run_ctors(app);

	typedef int (*entry_point_t)(int, char *[], char**);
	entry_point_t entry = (void*)(entryp);
#ifdef LD_EARLY_DEBUG
	close(emergency_fd);
#endif
	is_runtime = true;
	return entry(argc, argv, NULL);
}

/**
 * @brief Process LD_PRELOAD
 *
 * Load each of the requested libraries from LD_PRELOAD.
 */
static void do_preloads(void) {
	if (__ld_preload) {
		char * p = __ld_preload;
		while (*p) {
			while (*p == ' ' || *p == ':') p++;
			if (!*p) break;

			char *n = p;
			while (*n && *n != ' ' && *n != ':') n++;
			char s = *n;
			*n = '\0';

			if (!find_lib(p, NULL)) {
				dprintf("ld.so: '%s' from LD_PRELOAD could not be loaded: %s\n",
					p, __ld_error);
			}

			*n = s;
			p = n;
		}
	}
}

/**
 * @brief Load a main binary from an opened file.
 *
 * Used when ld.so is called directly with a path to a binary on the
 * command line. Performs the loading that would normally be done by
 * the kernel. If we're not running in ldd mode, also runs the app
 * normally - completing relocations, running constructors, and
 * jumping to its entry point.
 *
 * @param fd   Opened file to load (and run).
 * @param name Name of the binary for tracing.
 * @param argc Argument count to pass to entry point.
 * @param argv Argument vector to pass to entry point.
 * @returns Whatever the entry point returns, if it does, or 1
 *          in the case of failure to load the requested file.
 */
__attribute__((visibility("hidden")))
int __libc_load_from_file(int fd, const char * name, int argc, char *argv[]) {
	struct DlLib * app = try_load(name, fd, NULL, 1);

	if (!app) {
		dprintf("ld.so: nope\n");
		return 1;
	}

	if (unlikely(__is_ldd)) return 0;

	return run_app(app, argc, argv, app->base + app->ehdr->e_entry);
}

static uintptr_t auxv[32];

unsigned long getauxval(unsigned long type) {
	if (type < 32) return auxv[type];
	return 0;
}

/**
 * @brief Entry point of ld.so.
 *
 * This is the entry point called by the kernel when ld.so (libc.so)
 * is used as an interpreter or called as a main executable.
 *
 * Our ABI provides the normal platform argc/argv (and envp) arguments
 * in their C calling convention registers, which is probably very
 * weird but it works and I'm not going to change it any time soon.
 */
__attribute__((visibility("hidden")))
int __libc_start(int argc, char *argv[], char *envp[]) {
#ifdef LD_EARLY_DEBUG
	/* Set this if you expect ld.so to fail on init, before
	 * any files have been opened. Tries to ensure we have
	 * something to write logs to. */
	char _file[] = "/dev/ttyS0";
	emergency_fd = syscall__open(_file, O_WRONLY | O_CLOEXEC, 0);
#endif

	__envp = envp;

	/* Get auxv */
	int i;
	for (i = 0; envp[i]; ++i);
	uintptr_t * auxv_raw = (void*)(envp + i + 1);
	for (i = 0; auxv_raw[i]; i += 2) {
		if (auxv_raw[i] < 32) auxv[auxv_raw[i]] = auxv_raw[i+1];
	}

	/* Should we be in 'secure mode'? */
	if (auxv[AT_UID] != auxv[AT_EUID] || auxv[AT_GID] != auxv[AT_EGID] || auxv[AT_SECURE]) {
		target_is_suid = true;
	}

	/* Figure out what to do. */
	uintptr_t base = auxv[AT_BASE] ?: load_addr();

	uintptr_t dyn[32];
	for (i = 0; i<32; ++i) dyn[i] = 0;

	Elf64_Header * ehdr = (void*)base;
	Elf64_Phdr * phdrs = (void*)(base + ehdr->e_phoff);
	Elf64_Dyn * _ldso_dyn = NULL;

	for (size_t i = 0; i < ehdr->e_phnum; i++) {
		if (phdrs[i].p_type == PT_DYNAMIC) {
			Elf64_Dyn *_dyn = (void*)(phdrs[i].p_vaddr + base);
			_ldso_dyn = _dyn;
			while (_dyn->d_tag) {
				if (_dyn->d_tag < 32) dyn[_dyn->d_tag] = _dyn->d_un.d_val;
				_dyn++;
			}
			break;
		}
	}

	simple_relocs(dyn[DT_RELA] + base, dyn[DT_RELASZ], base, (void*)(dyn[DT_SYMTAB] + base));
	simple_relocs(dyn[DT_JMPREL] + base, dyn[DT_PLTRELSZ], base, (void*)(dyn[DT_SYMTAB] + base));

	bool show_auxv = !!simple_getenv("LD_SHOW_AUXV");
	if (show_auxv) {
		for (i = 0; auxv_raw[i]; i += 2) {
			switch (auxv_raw[i]) {
#define _fmt(n,fstr) case n: dprintf("%-16s %" fstr "\n", #n ":", auxv_raw[i+1]); break
				_fmt(AT_UID,"zu");
				_fmt(AT_EUID,"zu");
				_fmt(AT_GID,"zu");
				_fmt(AT_EGID,"zu");
				_fmt(AT_PAGESZ,"zu");
				_fmt(AT_RANDOM,"#zx");
				_fmt(AT_PHDR,"#zx");
				_fmt(AT_PHENT,"zu");
				_fmt(AT_PHNUM,"zu");
				_fmt(AT_BASE,"#zx");
				_fmt(AT_ENTRY,"#zx");
				default:
					dprintf("%#zx: %#zx\n", auxv_raw[i], auxv_raw[i+1]);
			}
		}
	}

	__trace_ld = !!simple_getenv("LD_DEBUG");
	if (!target_is_suid) __ld_preload = simple_getenv("LD_PRELOAD");
	__make_tls();

	extern char ** __argv;
	__argv = argv;

	struct DlLib * ldso = calloc(1, sizeof (struct DlLib));
	__libc_ldso = ldso;

	ldso->next = NULL;
	ldso->name = "libc.so";
	ldso->ehdr = ehdr;
	ldso->phdr = phdrs;
	ldso->phnum = ehdr->e_phnum;
	ldso->full_dyn = _ldso_dyn;
	ldso->base = base;
	memcpy(ldso->dyn, dyn, sizeof(dyn));
	ldso->strings = (void*)(ldso->base + ldso->dyn[DT_STRTAB]);
	ldso->syms    = (void*)(ldso->base + ldso->dyn[DT_SYMTAB]);
	ldso->hash    = (void*)(ldso->base + ldso->dyn[DT_HASH]);

	if (!auxv[AT_BASE]) {
		calculate_tls_size(ldso);
		run_ctors(ldso);
		__libc_init();
		return syscall__exit(__ld_so_main(argc, argv));
	}

	struct DlLib * app = calloc(1, sizeof (struct DlLib));
	app->name = argv[0];
	app->next = NULL;

	calculate_tls_size(app);
	calculate_tls_size(ldso);

	/* Okay let's try to actually relocate and link the binary the kernel loaded next to us... */
	phdrs = (void*)auxv[AT_PHDR];
	size_t phnum = auxv[AT_PHNUM];

	for (size_t i = 0; i < phnum; ++i) {
		if (phdrs[i].p_type == PT_PHDR) {
			app->base = auxv[AT_PHDR] - phdrs[i].p_vaddr;
			break;
		}
	}

	all_libraries = app;
	last_library = app;
	do_preloads();
	setup_lib(app, phdrs, phnum);

	return run_app(app, argc, argv, auxv[AT_ENTRY]);
}
