/**
 * @file kernel/misc/elf64.c
 * @brief Elf64 parsing tools for modules and static userspace binaries.
 *
 * Provides exec() for Elf64 binaries. Note that the loader only directly
 * loads static binaries; for dynamic binaries, the requested interpreter
 * is loaded, which should generally be /lib/ld.so, which should itself
 * be a static binary. This loader is platform-generic.
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

static Elf64_Shdr * elf_getSection(Elf64_Header * this, Elf64_Word index) {
	return (Elf64_Shdr*)((uintptr_t)this + this->e_shoff + index * this->e_shentsize);
}

static const char * sectionHeaderTypeToStr(Elf64_Word type) {
	static char buf[64];
	switch (type) {
		case SHT_NULL: return "NULL";
		case SHT_PROGBITS: return "PROGBITS";
		case SHT_SYMTAB: return "SYMTAB";
		case SHT_STRTAB: return "STRTAB";
		case SHT_RELA: return "RELA";
		case SHT_HASH: return "HASH";
		case SHT_DYNAMIC: return "DYNAMIC";
		case SHT_NOTE: return "NOTE";
		case SHT_NOBITS: return "NOBITS";
		case SHT_REL: return "REL";
		case SHT_SHLIB: return "SHLIB";
		case SHT_DYNSYM: return "DYNSYM";

		case 0xE: return "INIT_ARRAY";
		case 0xF: return "FINI_ARRAY";
		case 0x6ffffff6: return "GNU_HASH";
		case 0x6ffffffe: return "VERNEED";
		case 0x6fffffff: return "VERSYM";
		default:
			snprintf(buf, 63, "(%x)", type);
			return buf;
	}
}


int elf_module(const char * path) {
	Elf64_Header header;

	fs_node_t * file = kopen(path,0);

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
		return -EINVAL;
	}

	if (header.e_type != ET_REL) {
		printf("(Not a relocatable object)\n");
		return -EINVAL;
	}

	printf("Loading module...\n");

	/**
	 * Load the section string table, we'll want this for debugging, mostly.
	 */
	Elf64_Shdr shstr_hdr;
	read_fs(file, header.e_shoff + header.e_shentsize * header.e_shstrndx, sizeof(Elf64_Shdr), (uint8_t*)&shstr_hdr);

	char * stringTable = malloc(shstr_hdr.sh_size);
	read_fs(file, shstr_hdr.sh_offset, shstr_hdr.sh_size, (uint8_t*)stringTable);

	/**
	 * Modules are relocatable objects, so we have to link them by sections,
	 * not by PHDRs - they don't have those. We'll generally see a lot more
	 * relocations but usually of a smaller set.
	 */
	printf("\nSection Headers:\n");
	printf("  [Nr] Name              Type             Address           Offset\n");
	printf("       Size              EntSize          Flags  Link  Info  Align\n");
	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		Elf64_Shdr sectionHeader;
		read_fs(file, header.e_shoff + header.e_shentsize * i, sizeof(Elf64_Shdr), (uint8_t*)&sectionHeader);
		if (sectionHeader.sh_type == SHT_PROGBITS) {
			printf("progbits: %s\n", stringTable + sectionHeader.sh_name);
		} else if (sectionHeader.sh_type == SHT_NOBITS) {
			printf("nobits: %s\n", stringTable + sectionHeader.sh_name);
		}
		#if 0
		printf("  [%2d] %-17.17s %-16.16s %016lx  %08lx\n",
			i, stringTable + sectionHeader.sh_name, sectionHeaderTypeToStr(sectionHeader.sh_type),
			sectionHeader.sh_addr, sectionHeader.sh_offset);
		printf("       %016lx  %016lx %4ld %6d %5d %5ld\n",
			sectionHeader.sh_size, sectionHeader.sh_entsize, sectionHeader.sh_flags,
			sectionHeader.sh_link, sectionHeader.sh_info, sectionHeader.sh_addralign);
		#endif
	}

	/**
	 * In order to load a module, we need to link it as an object
	 * into the running kernel using the symbol table we integrated
	 * into our binary.
	 */
	//char * shrstrtab = (char*)elfHeader + elf_getSection(elfHeader, elfHeader->e_shstrndx)->sh_offset;

	/**
	 * First, we're going to check sections and update their addresses.
	 */
	#if 0
	for (unsigned int i = 0; i < elfHeader->e_shnum; ++i) {
		Elf64_Shdr * shdr = elf_getSection(elfHeader, i);
		if (shdr->sh_type == SHT_NOBITS) {
			if (shdr->sh_size) {
				printf("Warning: Module needs %lu bytes for BSS, we don't have an allocator.\n",
					shdr->sh_size);
			}
			/* otherwise, skip bss */
		} else {
			shdr->sh_addr = (uintptr_t)atAddress + shdr->sh_offset;
		}
	}
	#endif

	return 0;
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

	if (file->mask & 0x800) {
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
	process_release_directory(this_core->current_process->thread.page_directory);
	this_core->current_process->thread.page_directory = malloc(sizeof(page_directory_t));
	this_core->current_process->thread.page_directory->refcount = 1;
	spin_init(this_core->current_process->thread.page_directory->lock);
	this_core->current_process->thread.page_directory->directory = mmu_clone(NULL);
	mmu_set_directory(this_core->current_process->thread.page_directory->directory);

	for (int i = 0; i < header.e_phnum; ++i) {
		Elf64_Phdr phdr;
		read_fs(file, header.e_phoff + header.e_phentsize * i, sizeof(Elf64_Phdr), (uint8_t*)&phdr);
		if (phdr.p_type == PT_LOAD) {
			for (uintptr_t i = phdr.p_vaddr; i < phdr.p_vaddr + phdr.p_memsz; i += 0x1000) {
				union PML * page = mmu_get_page(i, MMU_GET_MAKE);
				mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
				mmu_invalidate(i);
			}

			read_fs(file, phdr.p_offset, phdr.p_filesz, (void*)phdr.p_vaddr);
			for (size_t i = phdr.p_filesz; i < phdr.p_memsz; ++i) {
				*(char*)(phdr.p_vaddr + i) = 0;
			}

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
	for (uintptr_t i = userstack - 64 * 0x400; i < userstack; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
		mmu_invalidate(i);
	}
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
