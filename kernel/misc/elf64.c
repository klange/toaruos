/**
 * @file kernel/misc/elf64.c
 * @brief Elf64 parsing tools for modules and static userspace binaries.
 *
 * Provides exec() for Elf64 binaries. Note that the loader only directly
 * loads static binaries; for dynamic binaries, the requested interpreter
 * is loaded, which should generally be /lib/ld.so, which should itself
 * be a static binary. This loader is platform-generic.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2023 K. Lange
 */
#include <bits/errno.h>
#include <kernel/types.h>
#include <kernel/symboltable.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/process.h>
#include <kernel/mmu.h>
#include <kernel/misc.h>
#include <kernel/ksym.h>
#include <kernel/module.h>
#include <kernel/hashmap.h>
#include <kernel/mutex.h>
#include <kernel/shm.h>
#include <kernel/mman.h>
#include <sys/auxv.h>
#include <sys/mman.h>

hashmap_t * _modules_table = NULL;
sched_mutex_t * _modules_mutex = NULL;

void modules_install(void) {
	_modules_table = hashmap_create(10);
	_modules_mutex = mutex_init("module loader");
}

hashmap_t * modules_get_list(void) {
	return _modules_table;
}

/**
 * Encode immediate for ADR(p) instruction
 */
static uint32_t aarch64_imm_adr(uint32_t val) {
	uint32_t low  = (val & 0x3) << 29;
	uint32_t high = ((val >> 2) & 0x7ffff) << 5;
	return low | high;
}

/**
 * Encode immediate for 12-bit instructions
 */
static uint32_t aarch64_imm_12(uint32_t val) {
	return (val & 0xFFF) << 10;
}

int elf_module(fs_node_t *file, int argc, char ** argv) {
	int error = 0;
	Elf64_Header header;

	ssize_t header_size = read_fs(file, 0, sizeof(Elf64_Header), (uint8_t*)&header);
	if (header_size != sizeof(Elf64_Header) ||
	    header.e_ident[0] != ELFMAG0 ||
	    header.e_ident[1] != ELFMAG1 ||
	    header.e_ident[2] != ELFMAG2 ||
	    header.e_ident[3] != ELFMAG3 ||
	    header.e_ident[EI_CLASS] != ELFCLASS64 ||
	    header.e_type != ET_REL ||
	    !header.e_shnum ||
	    header.e_shentsize != sizeof(Elf64_Shdr)) {
		return -ENOEXEC;
	}

	Elf64_Shdr * shdrs = malloc(header.e_shentsize * header.e_shnum);
	if (read_fs(file, header.e_shoff, header.e_shentsize * header.e_shnum, (void*)shdrs) != header.e_shentsize * header.e_shnum) {
		free(shdrs);
		return -EINVAL;
	}

	mutex_acquire(_modules_mutex);

	char * module_load_address = NULL;
	size_t module_size = 0;
	struct Module * moduleData = NULL;

	/*
	 * Loop over sections in two passes:
	 * First we calculate how much space we'll need to allocate for the module.
	 * Then we actually read the module / zero out NOBITS sections.
	 */
	for (int j = 0; j < 2; ++j) {
		size_t current_offset = 0;
		for (unsigned int i = 0; i < header.e_shnum; ++i) {

			/* Align this section as requested. */
			if (current_offset & (shdrs[i].sh_addralign - 1)) {
				current_offset = (current_offset + (shdrs[i].sh_addralign - 1)) & ~(shdrs[i].sh_addralign - 1);
			}

			if (module_load_address) {
				/* Second pass, actually load. */
				uintptr_t addr = (uintptr_t)module_load_address + current_offset;
				shdrs[i].sh_addr = addr;

				if (shdrs[i].sh_type == SHT_NOBITS) {
					memset((char*)addr, 0, shdrs[i].sh_size);
				} else {
					if (read_fs(file, shdrs[i].sh_offset, shdrs[i].sh_size, (void*)addr) != (ssize_t)shdrs[i].sh_size) {
						error = -EINVAL;
						goto _unmap_module;
					}
				}
			}

			current_offset += shdrs[i].sh_size;
		}

		/* Ensure the size we allocate is page aligned. */
		if (current_offset & (4095)) {
			current_offset = (current_offset + 4095) & ~4095;
		}

		if (!module_load_address) {
			/* First pass, allocate space. */
			module_size = current_offset;
			module_load_address = mmu_map_module(module_size);
		}
	}

	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		if (shdrs[i].sh_type != SHT_SYMTAB) continue;
		char * strtab = (char*)shdrs[shdrs[i].sh_link].sh_addr;
		Elf64_Sym * syms = (Elf64_Sym*)shdrs[i].sh_addr;

		for (unsigned int j = 0; j < shdrs[i].sh_size / sizeof(Elf64_Sym); ++j) {
			if (syms[j].st_shndx > 0 && syms[j].st_shndx < SHN_LOPROC) {
				/* Local */
				syms[j].st_value += shdrs[syms[j].st_shndx].sh_addr;
			} else if (syms[j].st_shndx == SHN_UNDEF) {
				/* Undefined global. */
				syms[j].st_value = (uintptr_t)ksym_lookup(strtab + syms[j].st_name);
			}

			if (syms[j].st_name && !strcmp(strtab + syms[j].st_name, "metadata")) {
				moduleData = (void*)syms[j].st_value;
			}
		}
	}

	if (!moduleData) {
		error = EINVAL;
		goto _unmap_module;
	}

	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		Elf64_Shdr * sectionHeader = &shdrs[i];
		if (sectionHeader->sh_type != SHT_RELA) continue;

		Elf64_Rela * table = (Elf64_Rela*)sectionHeader->sh_addr;

		/* Get the section these relocations apply to */
		Elf64_Shdr * targetSection = &shdrs[sectionHeader->sh_info];

		/* Get the symbol table */
		Elf64_Shdr * symbolSection = &shdrs[sectionHeader->sh_link];
		Elf64_Sym * symbolTable = (Elf64_Sym *)symbolSection->sh_addr;

#define S (symbolTable[ELF64_R_SYM(table[rela].r_info)].st_value)
#define A (table[rela].r_addend)
#define T32 (*(uint32_t*)target)
#define T64 (*(uint64_t*)target)
#define P  (target)

		for (unsigned int rela = 0; rela < sectionHeader->sh_size / sizeof(Elf64_Rela); ++rela) {
			uintptr_t target = table[rela].r_offset + targetSection->sh_addr;
			switch (ELF64_R_TYPE(table[rela].r_info)) {
#if defined(__x86_64__)
				case R_X86_64_64: {
					uint64_t sa = S + A;
					memcpy((void*)target, &sa, sizeof(uint64_t));
					break;
				}
				case R_X86_64_32: {
					uint32_t sa = S + A;
					memcpy((void*)target, &sa, sizeof(uint32_t));
					break;
				}
				case R_X86_64_PC32: {
					uint32_t samp = S + A - P;
					memcpy((void*)target, &samp, sizeof(uint32_t));
					break;
				}
#elif defined(__aarch64__)
				case R_AARCH64_ADR_PREL_PG_HI21: {
					T32 = T32 | aarch64_imm_adr( ((S + A) >> 12) - (P >> 12) );
					break;
				}
				case R_AARCH64_LDST64_ABS_LO12_NC:
					T32 = T32 | aarch64_imm_12( ((S + A) >> 3) & 0x1FF );
					break;
				case R_AARCH64_JUMP26:
				case R_AARCH64_CALL26:
					T32 = T32 | (((S + A - P) >> 2) & 0x3ffffff);
					break;
				case R_AARCH64_ABS64:
					T64 = S + A;
					break;
				case R_AARCH64_ABS32:
					T32 = S + A;
					break;
#endif
				default:
					dprintf("mod: unsupported relocation %ld found\n", ELF64_R_TYPE(table[rela].r_info));
					error = EINVAL;
			}
		}
	}

#undef S
#undef A
#undef T32
#undef T64
#undef P

	if (error) goto _unmap_module;

	if (hashmap_has(_modules_table, moduleData->name)) {
		error = EEXIST;
		goto _unmap_module;
	}

	struct LoadedModule * loadedData = malloc(sizeof(struct LoadedModule));
	loadedData->metadata = moduleData;
	loadedData->baseAddress = (uintptr_t)module_load_address;
	loadedData->fileSize = file->length;
	loadedData->loadedSize = module_size;

	free(shdrs);

	hashmap_set(_modules_table, moduleData->name, loadedData);
	mutex_release(_modules_mutex);

	/* Count arguments */
	return moduleData->init(argc, argv);

_unmap_module:
	free(shdrs);
	mmu_unmap_module((uintptr_t)module_load_address, module_size);

	mutex_release(_modules_mutex);
	return -error;
}

extern void process_acquire_big_lock(void);
extern void process_release_big_lock(void);

static uintptr_t load_from_file(fs_node_t * file, Elf64_Header * header, uintptr_t *base_out, int is_interp) {
	uintptr_t base = 0;
	uintptr_t phdr_vaddr = 0;

	if (header->e_type == ET_DYN) {
		base = is_interp ? 0x10000000 : 0x20000000;
	}

	for (int i = 0; i < header->e_phnum; ++i) {
		Elf64_Phdr phdr;
		read_fs(file, header->e_phoff + header->e_phentsize * i, sizeof(Elf64_Phdr), (uint8_t*)&phdr);
		if (phdr.p_type == PT_LOAD) {
			/* Round down */
			size_t    pageoffset = (phdr.p_vaddr & 0xFFF);
			uintptr_t addr   = phdr.p_vaddr - pageoffset;
			size_t    size   = phdr.p_filesz + pageoffset;
			off_t     offset = phdr.p_offset - pageoffset;
			size = (size + 0xFFF) & ~0xFFF;

			uintptr_t mapped_to = 0;
			int prot = PROT_READ;
			if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
			if (phdr.p_flags & PF_X) prot |= PROT_EXEC;

			if (size) {
				mapped_to = mmap_file(base + addr, size, prot, MAP_PRIVATE | MAP_FIXED, file, offset);
				if (phdr.p_flags & PF_W) {
					uintptr_t pad = mapped_to + pageoffset + phdr.p_filesz;
					if (pad & 0xFFF) {
						size_t fill = 0x1000 - (pad & 0xFFF);
						memset((void*)pad, 0, fill);
					}
				}
			} else {
				mapped_to = addr;
			}

			if (header->e_phoff >= phdr.p_offset && header->e_phoff < phdr.p_offset + phdr.p_filesz) {
				uintptr_t offset_into_segment = header->e_phoff - phdr.p_offset;
				phdr_vaddr = mapped_to + pageoffset + offset_into_segment;
			}

			if (phdr.p_memsz > phdr.p_filesz) {
				uintptr_t start = mapped_to + pageoffset + phdr.p_filesz;
				uintptr_t end   = mapped_to + pageoffset + phdr.p_memsz;
				uintptr_t start_page = (start + 0xFFF) & ~(0xFFF);
				uintptr_t end_page   = (end + 0xFFF) & ~(0xFFF);
				if (end_page > start_page) {
					mmap_anon(start_page, end_page - start_page, prot, MAP_PRIVATE | MAP_FIXED);
				}
			}
		}
	}

	*base_out = base;
	return phdr_vaddr;
}

int elf_exec(const char * path, fs_node_t * file, int argc, const char *const argv[], const char *const env[], int interp) {
	Elf64_Header header;

	read_fs(file, 0, sizeof(Elf64_Header), (uint8_t*)&header);

	if (header.e_ident[0] != ELFMAG0 ||
	    header.e_ident[1] != ELFMAG1 ||
	    header.e_ident[2] != ELFMAG2 ||
	    header.e_ident[3] != ELFMAG3) {
		printf("Invalid file: Bad header.\n");
		close_fs(file);
		return -EINVAL;
	}

	if (header.e_ident[EI_CLASS] != ELFCLASS64) {
		printf("(Wrong Elf class)\n");
		close_fs(file);
		return -EINVAL;
	}

	/* This loader can only handle basic executables. */
	if (header.e_type != ET_EXEC && header.e_type != ET_DYN) {
		close_fs(file);
		return -EINVAL;
	}

	fs_node_t * interpreter = NULL;
	Elf64_Header interp_header;

	for (int i = 0; i < header.e_phnum; ++i) {
		Elf64_Phdr phdr;
		read_fs(file, header.e_phoff + header.e_phentsize * i, sizeof(Elf64_Phdr), (uint8_t*)&phdr);
		if (phdr.p_type == PT_INTERP) {
			/* Must load interpreter */
			if (phdr.p_filesz < 2 || phdr.p_filesz > 256) return -EINVAL;
			char * tmp = malloc(phdr.p_filesz);
			read_fs(file, phdr.p_offset, phdr.p_filesz, (uint8_t*)tmp);
			if (tmp[phdr.p_filesz-1] != '\0') return free(tmp), -EINVAL;

			int error = 0;
			interpreter = kopen_error(tmp, 0, &error);
			free(tmp);
			if (!interpreter) return -error;

			ssize_t r = read_fs(interpreter, 0, sizeof(Elf64_Header), (uint8_t*)&interp_header);
			if (r < 0) return close_fs(interpreter), r;
			if ((size_t)r < sizeof(Elf64_Header)) return close_fs(interpreter), -EINVAL;

			if (interp_header.e_ident[0] != ELFMAG0 ||
			    interp_header.e_ident[1] != ELFMAG1 ||
			    interp_header.e_ident[2] != ELFMAG2 ||
			    interp_header.e_ident[3] != ELFMAG3 ||
			    interp_header.e_ident[EI_CLASS] != ELFCLASS64 ||
			    (interp_header.e_type != ET_EXEC && interp_header.e_type != ET_DYN)) {
				return close_fs(interpreter), -EINVAL;
			}
		}
	}

	/* Point of no return. */

	if (!(this_core->current_process->flags & (PROC_FLAGS_TRACE)) && !this_core->current_process->tracer) {
		if (file->mask & S_ISUID) this_core->current_process->user = file->uid; /* set-user-ID */
		if (file->mask & S_ISGID) this_core->current_process->user_group = file->gid; /* set-group-ID */
	}

	this_core->current_process->saved_user = this_core->current_process->user;
	this_core->current_process->saved_user_group = this_core->current_process->user_group;

	shm_release_all((process_t *)this_core->current_process);
	process_close_fds((process_t *)this_core->current_process, PROC_FD_MODE_CLOEXEC);

	process_acquire_big_lock();
	mmu_set_directory(NULL);
	page_directory_t * this_directory = this_core->current_process->thread.page_directory;
	this_core->current_process->thread.page_directory = calloc(1, sizeof(page_directory_t));
	this_core->current_process->thread.page_directory->refcount = 1;
	spin_init(this_core->current_process->thread.page_directory->lock);
	this_core->current_process->thread.page_directory->directory = mmu_clone(NULL);
	mmu_set_directory(this_core->current_process->thread.page_directory->directory);
	process_release_directory(this_directory);
	process_release_big_lock();

	for (int i = 0; i < NUMSIGNALS; ++i) {
		if (this_core->current_process->signals[i].handler != 1) {
			this_core->current_process->signals[i].handler = 0;
			this_core->current_process->signals[i].flags = 0;
		}
	}

	/* Load binary */
	uintptr_t base_addr;
	uintptr_t phdr_vaddr = load_from_file(file, &header, &base_addr, 0);
	uintptr_t entrypoint = header.e_entry + base_addr;
	uintptr_t interp_base = 0;
	close_fs(file);

	/* We've loaded the binary, now let's load the interpreter! */
	if (interpreter) {
		load_from_file(interpreter, &interp_header, &interp_base, 1);
		entrypoint = interp_base + interp_header.e_entry;
		close_fs(interpreter);
	}

	extern uint32_t rand(void);

	#if 0
	uintptr_t shake = (rand() & 0x7FFF) * 0x100000;
	this_core->current_process->image.heap  = 0x100000000 + shake;
	#endif
	/* TODO some stuff breaks with the bigger heap */
	this_core->current_process->image.heap  = 0x60000000;
	this_core->current_process->image.entry = entrypoint;

	// arch_set_...?

	/* Map stack space */
	uintptr_t userstack = 0x800000000000;
	size_t    stack_size = 512 * 0x400;
	mmap_anon(userstack - stack_size, stack_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED);
	this_core->current_process->image.userstack = userstack;

#define PUSH(type,val) do { \
	userstack -= sizeof(type); \
	while (userstack & (sizeof(type)-1)) userstack--; \
	*((type*)userstack) = (val); \
} while (0)
#define PUSHSTR(s) do { \
	ssize_t l = strlen(s); \
	do { \
		PUSH(char,s[l]); \
		l--; \
	} while (l>=0); \
} while (0)

	char * argv_ptrs[argc];
	for (int i = 0; i < argc; ++i) {
		PUSHSTR(argv[i]);
		argv_ptrs[i] = (char*)userstack;
	}

	/* Now push envp */
	int envc = 0;
	char ** envpp = (char**)env;
	while (*envpp) {
		envc++;
		envpp++;
	}
	char * envp_ptrs[envc];
	for (int i = 0; i < envc; ++i) {
		PUSHSTR(env[i]);
		envp_ptrs[i] = (char*)userstack;
	}

	PUSH(uint32_t, rand());
	PUSH(uint32_t, rand());
	PUSH(uint32_t, rand());
	PUSH(uint32_t, rand());
	uintptr_t rand_offset = userstack;

#define push_auxv(name, value) do { \
	PUSH(uintptr_t, (uintptr_t)(value)); \
	PUSH(uintptr_t, (uintptr_t)(name)); \
} while (0)

	push_auxv(AT_NULL,   NULL);
	push_auxv(AT_RANDOM, rand_offset);
	push_auxv(AT_EGID,   this_core->current_process->user_group);
	push_auxv(AT_GID,    this_core->current_process->real_user_group);
	push_auxv(AT_EUID,   this_core->current_process->user);
	push_auxv(AT_UID,    this_core->current_process->real_user);
	push_auxv(AT_PHNUM,  header.e_phnum);
	push_auxv(AT_PHENT,  sizeof(Elf64_Phdr));
	push_auxv(AT_PHDR,   phdr_vaddr);
	push_auxv(AT_ENTRY,  base_addr + header.e_entry);
	push_auxv(AT_BASE,   interp_base);
	push_auxv(AT_PAGESZ, 4096);

	PUSH(uintptr_t, 0); /* envp NULL */
	for (int i = envc; i > 0; i--) {
		PUSH(char*,envp_ptrs[i-1]);
	}
	char ** _envp = (char**)userstack;
	PUSH(uintptr_t, 0); /* argv NULL */
	for (int i = argc; i > 0; i--) {
		PUSH(char*,argv_ptrs[i-1]);
	}
	char ** _argv = (char**)userstack;
	PUSH(uintptr_t, argc);

	arch_set_kernel_stack(this_core->current_process->image.stack);
	arch_enter_user(entrypoint, argc, _argv, _envp, userstack);

	task_exit(127 << 8);
	__builtin_unreachable();
}
