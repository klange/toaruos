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
#include <errno.h>
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

int elf_module(char ** args) {
	int error = 0;
	Elf64_Header header;

	fs_node_t * file = kopen(args[0],0);

	if (!file) {
		return -ENOENT;
	}

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

	if (header.e_type != ET_REL) {
		printf("(Not a relocatable object)\n");
		close_fs(file);
		return -EINVAL;
	}

	mutex_acquire(_modules_mutex);

	/* Just slap the whole thing into memory, why not... */
	char * module_load_address = mmu_map_module(file->length);
	read_fs(file, 0, file->length, (void*)module_load_address);

	/**
	 * Set up section header entries to have correct loaded addresses, and map
	 * any NOBITS sections to new memory. We'll page-align anything, which
	 * should be good enough for any object files we make...
	 */
	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		Elf64_Shdr * sectionHeader = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * i);
		if (sectionHeader->sh_type == SHT_NOBITS) {
			sectionHeader->sh_addr = (uintptr_t)mmu_map_module(sectionHeader->sh_size);
			memset((void*)sectionHeader->sh_addr, 0, sectionHeader->sh_size);
		} else {
			sectionHeader->sh_addr = (uintptr_t)(module_load_address + sectionHeader->sh_offset);
			if (sectionHeader->sh_addralign &&
				(sectionHeader->sh_addr & (sectionHeader->sh_addralign -1))) {
				dprintf("mod: probably not aligned correctly: %#zx %ld\n",
					sectionHeader->sh_addr, sectionHeader->sh_addralign);
			}
		}
	}

	struct Module * moduleData = NULL;

	/**
	 * Let's start loading symbols...
	 */
	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		Elf64_Shdr * sectionHeader = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * i);
		if (sectionHeader->sh_type != SHT_SYMTAB) continue;
		Elf64_Shdr * strtab_hdr = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * sectionHeader->sh_link);
		char * symNames = (char*)strtab_hdr->sh_addr;
		Elf64_Sym * symTable = (Elf64_Sym*)sectionHeader->sh_addr;
		/* Uh, we should be able to figure out how many symbols we have by doing something less dumb than
		 * just checking the size of the section, right? */
		for (unsigned int sym = 0; sym < sectionHeader->sh_size / sizeof(Elf64_Sym); ++sym) {
			/* Unlike the previous implementation of this module loader in toaru32,
			 * we specifically do not support binding symbols directly from newly
			 * loaded modules. If a module wants to expose symbols, it should use
			 * @c ksym_bind to supply new symbol names to the symbol table. */

			if (symTable[sym].st_shndx > 0 && symTable[sym].st_shndx < SHN_LOPROC) {
				Elf64_Shdr * sh_hdr = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * symTable[sym].st_shndx);
				symTable[sym].st_value = symTable[sym].st_value + sh_hdr->sh_addr;
			} else if (symTable[sym].st_shndx == SHN_UNDEF) {
				symTable[sym].st_value = (uintptr_t)ksym_lookup(symNames + symTable[sym].st_name);
			}

			if (symTable[sym].st_name && !strcmp(symNames + symTable[sym].st_name, "metadata")) {
				moduleData = (void*)symTable[sym].st_value;
			}
		}
	}

	if (!moduleData) {
		error = EINVAL;
		goto _unmap_module;
	}

	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		Elf64_Shdr * sectionHeader = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * i);
		if (sectionHeader->sh_type != SHT_RELA) continue;

		Elf64_Rela * table = (Elf64_Rela*)sectionHeader->sh_addr;

		/* Get the section these relocations apply to */
		Elf64_Shdr * targetSection = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * sectionHeader->sh_info);

		/* Get the symbol table */
		Elf64_Shdr * symbolSection = (Elf64_Shdr*)(module_load_address + header.e_shoff + header.e_shentsize * sectionHeader->sh_link);
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
				case R_X86_64_64:
					T64 = S + A;
					break;
				case R_X86_64_32:
					T32 = S + A;
					break;
				case R_X86_64_PC32:
					T32 = S + A - P;
					break;
#elif defined(__aarch64__)
				case 275: { /* R_AARCH64_ADR_PREL_PG_HI21 */
					T32 = T32 | aarch64_imm_adr( ((S + A) >> 12) - (P >> 12) );
					break;
				}
				case 286: /* R_AARCH64_LDST64_ABS_LO12_NC */
					T32 = T32 | aarch64_imm_12( ((S + A) >> 3) & 0x1FF );
					break;
				case 282: /* R_AARCH64_JUMP26 */
				case 283: /* R_AARCH64_CALL26 */
					T32 = T32 | (((S + A - P) >> 2) & 0x3ffffff);
					break;
				case 257: /* ABS64 */
					T64 = S + A;
					break;
				case 258: /* ABS32 */
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
	loadedData->loadedSize = (uintptr_t)mmu_map_module(0) - (uintptr_t)module_load_address;

	close_fs(file);

	hashmap_set(_modules_table, moduleData->name, loadedData);
	mutex_release(_modules_mutex);

	/* Count arguments */
	int argc = 0;
	for (char ** aa = args; *aa; ++aa) ++argc;

	return moduleData->init(argc, args);

_unmap_module:
	close_fs(file);

	mmu_unmap_module((uintptr_t)module_load_address, (uintptr_t)mmu_map_module(0) - (uintptr_t)module_load_address);

	mutex_release(_modules_mutex);
	return -error;
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
		return -EINVAL;
	}

	/* This loader can only handle basic executables. */
	if (header.e_type != ET_EXEC) {
		printf("(Not an executable)\n");
		/* TODO: what about DYN? */
		return -EINVAL;
	}

	if ((file->mask & S_ISUID) && !(this_core->current_process->flags & (PROC_FLAG_TRACE_SYSCALLS | PROC_FLAG_TRACE_SIGNALS))) {
		/* setuid */
		this_core->current_process->user = file->uid;
	}

	/* First check if it is dynamic and needs an interpreter */
	for (int i = 0; i < header.e_phnum; ++i) {
		Elf64_Phdr phdr;
		read_fs(file, header.e_phoff + header.e_phentsize * i, sizeof(Elf64_Phdr), (uint8_t*)&phdr);
		if (phdr.p_type == PT_DYNAMIC) {
			close_fs(file);
			unsigned int nargc = argc + 3;
			const char * args[nargc+1]; /* oh yeah, great, a stack-allocated dynamic array... wonderful... */
			args[0] = "ld.so";
			args[1] = "-e";
			args[2] = strdup(this_core->current_process->name);
			int j = 3;
			for (int i = 0; i < argc; ++i, ++j) {
				args[j] = argv[i];
			}
			args[j] = NULL;
			fs_node_t * file = kopen("/lib/ld.so",0); /* FIXME PT_INTERP value */
			if (!file) return -EINVAL;
			return elf_exec(NULL, file, nargc, args, env, 1);
		}
	}

	uintptr_t execBase = -1;
	uintptr_t heapBase = 0;

	mmu_set_directory(NULL);
	page_directory_t * this_directory = this_core->current_process->thread.page_directory;
	this_core->current_process->thread.page_directory = malloc(sizeof(page_directory_t));
	this_core->current_process->thread.page_directory->refcount = 1;
	spin_init(this_core->current_process->thread.page_directory->lock);
	this_core->current_process->thread.page_directory->directory = mmu_clone(NULL);
	mmu_set_directory(this_core->current_process->thread.page_directory->directory);
	process_release_directory(this_directory);
	for (int i = 0; i < NUMSIGNALS; ++i) {
		if (this_core->current_process->signals[i].handler != 1) {
			this_core->current_process->signals[i].handler = 0;
			this_core->current_process->signals[i].flags = 0;
		}
	}

	for (int i = 0; i < header.e_phnum; ++i) {
		Elf64_Phdr phdr;
		read_fs(file, header.e_phoff + header.e_phentsize * i, sizeof(Elf64_Phdr), (uint8_t*)&phdr);
		if (phdr.p_type == PT_LOAD) {
			for (uintptr_t i = phdr.p_vaddr; i < phdr.p_vaddr + phdr.p_memsz; i += 0x1000) {
				union PML * page = mmu_get_page(i, MMU_GET_MAKE);
				mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
			}

			read_fs(file, phdr.p_offset, phdr.p_filesz, (void*)phdr.p_vaddr);
			for (size_t i = phdr.p_filesz; i < phdr.p_memsz; ++i) {
				*(char*)(phdr.p_vaddr + i) = 0;
			}

			#ifdef __aarch64__
			extern void arch_clear_icache(uintptr_t,uintptr_t);
			arch_clear_icache(phdr.p_vaddr, phdr.p_vaddr + phdr.p_memsz);
			#endif

			if (phdr.p_vaddr + phdr.p_memsz > heapBase) {
				heapBase = phdr.p_vaddr + phdr.p_memsz;
			}

			if (phdr.p_vaddr < execBase) {
				execBase = phdr.p_vaddr;
			}
		}
		/* TODO: Should also be setting up TLS PHDRs. */
	}

	this_core->current_process->image.heap  = (heapBase + 0xFFF) & (~0xFFF);
	this_core->current_process->image.entry = header.e_entry;

	close_fs(file);

	// arch_set_...?

	/* Map stack space */
	uintptr_t userstack = 0x800000000000;
	for (uintptr_t i = userstack - 512 * 0x400; i < userstack; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
	}

	this_core->current_process->image.userstack = userstack - 16 * 0x400;

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

	/* XXX This should probably be done backwards so we can be
	 *     sure that we're aligning the stack properly. It
	 *     doesn't matter too much as our crt0+libc align it
	 *     correctly for us and environ + auxv detection is
	 *     based on the addresses of argv, not the actual
	 *     stack pointer, but it's still weird. */
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

	PUSH(uintptr_t, 0);
	PUSH(uintptr_t, this_core->current_process->user);
	PUSH(uintptr_t, 11); /* AT_UID */
	PUSH(uintptr_t, this_core->current_process->real_user);
	PUSH(uintptr_t, 12); /* AT_EUID */
	PUSH(uintptr_t, 0);

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
	arch_enter_user(header.e_entry, argc, _argv, _envp, userstack);

	return -EINVAL;
}
