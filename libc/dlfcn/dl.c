/**
 * @file libc/dlfcn/dl.c
 * @brief ld.so entry point
 *
 * Self-relocates ld.so (actually libc.so).
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
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <va_list.h>
#include <kernel/elf.h>

#include <syscall.h>
#include <syscall_nums.h>

#ifdef LD_EARLY_DEBUG
static DEFN_SYSCALL3(_open,SYS_OPEN,const char*,long,mode_t);
static int emergency_fd = 0;
#endif

struct DlLib {
	const char   * name;
	Elf64_Header * ehdr;
	Elf64_Phdr   * phdr;
	size_t       phnum;
	Elf64_Dyn    * full_dyn;
	Elf64_Sym    * syms;
	const char   * strings;
	Elf64_Word   * hash;
	struct DlLib * next;
	struct DlLib * next_syms;
	uintptr_t    base;
	uintptr_t    dyn[32];
	uintptr_t    tlsbase;
	size_t       tlssize;
	bool         relocated;
	bool         constructed;

	struct DlLib **dependencies;
};

static struct DlLib *all_libraries = NULL;
static struct DlLib *last_library = NULL;
static struct DlLib *__libc_ldso = NULL;
static size_t current_tls_offset = 16;
static bool is_runtime = false;
static bool target_is_suid = false;
static char ** __envp = NULL;

static DEFN_SYSCALL1(_exit,SYS_EXT,int);

extern char __ehdr_start[] __attribute__((weak, visibility("hidden")));

static void simple_memcpy(char *a, char * b, size_t sz) {
	while (sz) {
		*a++ = *b++;
		sz--;
	}
}

static char * simple_getenv(const char * var) {
	char ** envp = __envp;
	size_t len = strlen(var);
	for (char * e; (e = *envp); envp++) if (!strncmp(*envp,var,len) && e[len] == '=') return &e[len+1];
	return NULL;
}

static uintptr_t load_addr(void) {
	uintptr_t out;
#if defined(__aarch64__)
	__asm__(
	"  adrp %0, __ehdr_start\n"
	"  add %0, %0, #:lo12:__ehdr_start\n"
	:"=r"(out));
#else
	out = (uintptr_t)&__ehdr_start;
#endif
	return out;
}

void __register_frame_info(void) {
}
void __deregister_frame_info(void) {
}

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

static Elf64_Sym * elf_sym_lookup(Elf64_Word *table, const char *strtab, Elf64_Sym *symtab, const char *name, Elf64_Word h) {
	Elf64_Word nbuckets = table[0];
	for (size_t i = table[2 + h % nbuckets]; i; i = table[2 + nbuckets + i]) {
		if (!strcmp(strtab + symtab[i].st_name, name)) return symtab + i;
	}
	return NULL;
}


extern size_t xvasprintf(int (*callback)(void *, char), void * userData, const char * fmt, va_list args);

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

static int dprintf(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(cb_dprintf, NULL, fmt, args);
	va_end(args);
	return out;
}

__attribute__((unused))
static size_t __tlsdesc_static(size_t * a) {
	return a[1];
}

static inline int is_copy(int type) {
#if defined(__aarch64__)
	return type == R_AARCH64_COPY;
#elif defined(__x86_64__)
	return type == R_X86_64_COPY;
#else
# error "Unknown arch"
#endif
}

static inline int is_tlsoff(int type) {
#if defined(__aarch64__)
	return type == R_AARCH64_TLS_TPREL;
#elif defined(__x86_64__)
	return type == R_X86_64_TPOFF64;
#else
# error "Unknown arch"
#endif
}

void _ITM_registerTMCloneTable(void) {}
void _ITM_deregisterTMCloneTable(void) {}
void __cxa_finalize(void) {}


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

			if (sym) {
				x = sym->st_shndx == (SHN_ABS ? 0 : their_base) + sym->st_value;

				if ((sym->st_info >> 4) == STB_LOCAL) {
					//dprintf("Local\n");
				} else {
					/* Try to resolve it from libc ?*/
					const char *name = strtab + sym->st_name;
					Elf64_Word hash = elf_hash(name);


					for (struct DlLib * dep = (is_copy(type) ? lib->next : all_libraries); dep; dep = dep->next) {
						Elf64_Sym *maybe = elf_sym_lookup(dep->hash, dep->strings, dep->syms, name, hash);
						if (maybe && maybe->st_shndx != SHN_UNDEF) {
							resolved = maybe;
							inlib = dep;
							break;
						}
					}

					if (resolved) {
						//dprintf("Resolved symbol '%s' as %#zx\n",
						//	name, (inlib->base + resolved->st_value));
						x = inlib->base + resolved->st_value;
						tlsx = inlib->tlsbase + resolved->st_value;
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
static size_t calculate_tls_size(struct DlLib * lib);

static struct DlLib * try_load(const char * name, int fd, struct DlLib * parent, int is_exec) {
	struct DlLib * lib = calloc(1, sizeof(struct DlLib));

	size_t avail = sizeof(struct Elf64_Header) + 300;
	Elf64_Header * lib_header = calloc(1, avail);
	ssize_t r = pread(fd, lib_header, avail, 0);
	if (r < 0 || (size_t)r < avail) goto _fail_dep;
	if (lib_header->e_type != ET_DYN && (!is_exec || lib_header->e_type != ET_EXEC)) goto _fail_dep;
	if (lib_header->e_phoff + lib_header->e_phentsize > avail) {
		dprintf("ld.so: %s: need to load more phdrs, failing for now\n", name);
		goto _fail_dep;
	}

	uintptr_t base_addr = (uintptr_t)-1;
	uintptr_t end_addr = 0x0;

	for (size_t i = 0; i < lib_header->e_phnum; ++i) {
		Elf64_Phdr * phdr = (void*)((uintptr_t)lib_header + lib_header->e_phoff + lib_header->e_phentsize * i);
		if (phdr->p_type != PT_LOAD) continue;

		#if 0
		dprintf("Load vaddr=%#zx, size=%zu, offset=%zu, filesz=%zu\n",
			phdr->p_vaddr, phdr->p_memsz, phdr->p_offset, phdr->p_filesz);
		#endif

		if ((phdr->p_vaddr & ~0xFFF) < base_addr) base_addr = (phdr->p_vaddr & ~0xFFF);
		if (phdr->p_vaddr + phdr->p_memsz > end_addr) end_addr = phdr->p_vaddr + phdr->p_memsz;
	}

	uintptr_t load_addr = is_exec ? 0 : (uintptr_t)valloc(end_addr - base_addr);

	for (size_t i = 0; i < lib_header->e_phnum; ++i) {
		Elf64_Phdr * phdr = (void*)((uintptr_t)lib_header + lib_header->e_phoff + lib_header->e_phentsize * i);
		if (phdr->p_type != PT_LOAD) continue;

		size_t    pageoffset = ((load_addr + phdr->p_vaddr) & 0xFFF);
		uintptr_t addr   = load_addr + phdr->p_vaddr - pageoffset;
		size_t    size   = phdr->p_filesz + pageoffset;
		off_t     offset = phdr->p_offset - pageoffset;
		size = (size + 0xFFF) & ~0xFFF;

		int prot = PROT_READ;
		if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
		if (phdr->p_flags & PF_X) prot |= PROT_EXEC;

		char * mapped_to = mmap((void*)addr, size, prot, MAP_PRIVATE | MAP_FIXED, fd, offset);
		if (phdr->p_flags & PF_W) {
			uintptr_t pad = (uintptr_t)mapped_to + pageoffset + phdr->p_filesz;
			if (pad & 0xFFF) {
				size_t fill = 0x1000 - (pad & 0xFFF);
				memset((void*)pad, 0, fill);
			}
		}

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

	close(fd);

	lib->name = strdup(name);
	lib->ehdr = lib_header;
	lib->base = load_addr;
	lib->tlsbase = current_tls_offset;

	Elf64_Phdr * phdrs = (void*)((uintptr_t)lib_header + lib_header->e_phoff);

	if (is_exec) {
		all_libraries = lib;
		lib->next = __libc_ldso;
	} else {
		lib->next = NULL;
		last_library->next = lib;
		last_library = lib;
	}

	//dprintf("Setting up %s\n", name);
	setup_lib(lib, phdrs, lib_header->e_phnum);

	return lib;

_fail_dep:
	dprintf("ld.so: failed to load dependency '%s'\n", name);
	free(lib_header);
	free(lib);
	close(fd);
	return NULL;
}

static struct DlLib * find_lib(const char * name, struct DlLib * parent) {
	if (!strcmp(name,"libc.so")) return __libc_ldso;

	struct DlLib * ptr = all_libraries;
	while (ptr) {
		if (!strcmp(ptr->name, name)) return ptr;
		ptr = ptr->next;
	}

	if (name[0] == '/') {
		int fd = open(name, O_RDONLY | O_CLOEXEC);
		if (fd < 0) return NULL;

		return try_load(name, fd, parent, 0);
	}

	/* Did not find it, let's go load it. */
	char * path = "/lib:/usr/lib";

	if (!target_is_suid) {
		char * p = simple_getenv("LD_LIBRARY_PATH");
		if (p) path = p;
	}

	char * xpath = strdup(path);
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

		/* Load file */
		int fd = open(exe, O_RDONLY | O_CLOEXEC);
		free(exe);

		if (fd < 0) continue; /* set error and break? */

		struct DlLib * maybe = try_load(name, fd, parent, 0);
		if (maybe) {
			free(xpath);
			return maybe;
		}
	}

	free(xpath);

	return NULL;
}

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

	app->dependencies = calloc(count, sizeof(struct DlLib *));
	count = 0;

	for (Elf64_Dyn *d = app->full_dyn; d->d_tag; d++) {
		if (d->d_tag != DT_NEEDED) continue;
		const char * name = app->strings + d->d_un.d_val;
		//dprintf("Need '%s'\n", name);
		struct DlLib * dep = find_lib(name, app);
		app->dependencies[count++] = dep;
	}
	
	current_tls_offset += calculate_tls_size(app);
}

static void relocate_stuff(struct DlLib *lib) {
	for (; lib; lib = lib->next) {
		if (lib->relocated) continue;
		relocate(lib);
		lib->relocated = true;
	}
}

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

void * dlsym(struct DlLib * lib, const char * name) {
	Elf64_Word hash = elf_hash(name);

	if (lib == NULL) lib = all_libraries;

	for (struct DlLib * dep = lib; dep; dep = dep->next) {
		Elf64_Sym *maybe = elf_sym_lookup(dep->hash, dep->strings, dep->syms, name, hash);
		if (maybe && maybe->st_shndx != SHN_UNDEF) {
			return (void*)(dep->base + maybe->st_value);
		}
	}

	return NULL;
}

static size_t calculate_tls_size(struct DlLib * lib) {
	uintptr_t * their_dyn = lib->dyn;
	uintptr_t   their_base = lib->base;
	Elf64_Sym  *symtab = lib->syms;

	size_t tls_size = 0;

	size_t size = their_dyn[DT_RELASZ];
	uintptr_t reltable = their_base + their_dyn[DT_RELA];
	for (int i = 0; i < 2; ++i) {
		Elf64_Rela *table  = (void*)reltable;
		while ((uintptr_t)table - reltable < size) {
			Elf64_Word symbol = ELF64_R_SYM(table->r_info);
			Elf64_Word type   = ELF64_R_TYPE(table->r_info);
			Elf64_Sym  *sym   = symbol ? &symtab[symbol] : NULL;
			if (!is_tlsoff(type)) goto _continue;
			tls_size += sym->st_size;
_continue:
			table++;
		}
		if (i == 1) break;
		reltable = their_base + their_dyn[DT_JMPREL];
		size  = their_dyn[DT_PLTRELSZ];
	}
	lib->tlssize = tls_size;
	return tls_size;
}

static int run_app(struct DlLib * app, int argc, char * argv[], uintptr_t entryp) {
	relocate_stuff(app->next);
	relocate_stuff(app);

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

__attribute__((visibility("hidden")))
int __libc_load_from_file(int fd, const char * name, int argc, char *argv[]) {
	last_library = __libc_ldso;

	struct DlLib * app = try_load(name, fd, NULL, 1);

	if (!app) {
		dprintf("ld.so: nope\n");
		return 1;
	}

	return run_app(app, argc, argv, app->base + app->ehdr->e_entry);
}

__attribute__((visibility("hidden")))
int __libc_start(int argc, char *argv[], char *envp[]) {
#ifdef LD_EARLY_DEBUG
# if defined(__aarch64__)
	char _file[] = "/dev/ttyS0";
# else
	char _file[] = "/dev/ttyS1";
#endif
	emergency_fd = syscall__open(_file, O_WRONLY | O_CLOEXEC, 0);
#endif

	__envp = envp;

	/* Get auxv */
	int i;
	for (i = 0; envp[i]; ++i);
	uintptr_t * auxv_raw = (void*)(envp + i + 1);
	uintptr_t auxv[32];
	for (i = 0; i<32; ++i) auxv[i] = 0;
	for (i = 0; auxv_raw[i]; i += 2) {
		if (auxv_raw[i] < 32) auxv[auxv_raw[i]] = auxv_raw[i+1];
	}

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
#define _fmt(n,fstr) case n: dprintf("%16s %" fstr "\n", #n ":", auxv_raw[i+1]); break
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
	ldso->tlsbase = current_tls_offset;

	if (!auxv[AT_BASE]) {
		run_ctors(ldso);
		extern void _init(void);
		_init();

		extern int ld_so_main(int, char**);
		return syscall__exit(ld_so_main(argc, argv));
	}

	current_tls_offset += calculate_tls_size(ldso);

	struct DlLib * app = calloc(1, sizeof (struct DlLib));
	app->name = argv[0];
	app->next = ldso;
	app->tlsbase = current_tls_offset;

	all_libraries = app;
	last_library = ldso;

	/* Okay let's try to actually relocate and link the binary the kernel loaded next to us... */
	phdrs = (void*)auxv[AT_PHDR];
	size_t phnum = auxv[AT_PHNUM];

	for (size_t i = 0; i < phnum; ++i) {
		if (phdrs[i].p_type == PT_PHDR) {
			app->base = auxv[AT_PHDR] - phdrs[i].p_vaddr;
			break;
		}
	}

	setup_lib(app, phdrs, phnum);

	return run_app(app, argc, argv, auxv[AT_ENTRY]);
}
