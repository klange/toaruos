/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * ELF Binary Executable headers
 *
 */

#ifndef _ELF_H
#define _ELF_H

/*
 * Different bits of our build environment
 * require different header files for definitions
 */
#ifdef _KERNEL_
#	include <types.h>
#else
#	include <stdint.h>
#endif

/*
 * Unless otherwise stated, the definitions herein
 * are sourced from the Portable Formats Specification,
 * version 1.1 - ELF: Executable and Linkable Format
 */

/*
 * ELF Magic Signature
 */
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define EI_NIDENT 16

/*
 * ELF Datatypes
 */
typedef uint32_t Elf32_Word;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint32_t Elf32_Sword;
typedef uint16_t Elf32_Half;

/*
 * ELF Header
 */
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half    e_type;
	Elf32_Half    e_machine;
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;
	Elf32_Off     e_phoff;
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;
	Elf32_Half    e_shentsize;
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
} Elf32_Header;

/*
 * e_type
 */

#define ET_NONE   0     /* No file type */
#define ET_REL    1     /* Relocatable file */
#define ET_EXEC   2     /* Executable file */
#define ET_DYN    3     /* Shared object file */
#define ET_CORE   4     /* Core file */
#define ET_LOPROC 0xff0 /* [Processor Specific] */
#define ET_HIPROC 0xfff /* [Processor Specific] */

/*
 * Machine types
 */
#define EM_NONE  0
#define EM_386   3

#define EV_NONE    0
#define EV_CURRENT 1

/** Program Header */
typedef struct {
	Elf32_Word p_type;
	Elf32_Off  p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
} Elf32_Phdr;

/* p_type values */
#define PT_NULL    0 /* Unused, skip me */
#define PT_LOAD    1 /* Loadable segment */
#define PT_DYNAMIC 2 /* Dynamic linking information */
#define PT_INTERP  3 /* Interpreter (null-terminated string, pathname) */
#define PT_NOTE    4 /* Auxillary information */
#define PT_SHLIB   5 /* Reserved. */
#define PT_PHDR    6 /* Oh, it's me. Hello! Back-reference to the header table itself */
#define PT_LOPROC  0x70000000
#define PT_HIPROC  0x7FFFFFFF


/** Section Header */
typedef struct {
	Elf32_Word sh_name;
	Elf32_Word sh_type;
	Elf32_Word sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off  sh_offset;
	Elf32_Word sh_size;
	Elf32_Word sh_link;
	Elf32_Word sh_info;
	Elf32_Word sh_addralign;
	Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct {
	uint32_t  id;
	uintptr_t ptr;
} Elf32_auxv;

typedef struct {
	Elf32_Word st_name;
	Elf32_Addr st_value;
	Elf32_Word st_size;
	unsigned char st_info;
	unsigned char st_other;
	Elf32_Half st_shndx;
} Elf32_Sym;

typedef struct {
	Elf32_Addr r_offset;
	Elf32_Word r_info;
} Elf32_Rel;

/* sh_type values */
#define SHT_NONE     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_NOBITS   8
#define SHT_REL      9

#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((unsigned char)(i))
#define ELF32_R_INFO(s,t) (((s) << 8) + (unsigned char)(t))

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0xf)
#define ELF32_ST_INFO(b,t) (((b) << 4) + ((t) & 0xf))

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STB_NUM    3

#define STB_LOPROC 13
#define STB_HIPROC 15

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6
#define STT_NUM     7

#define STT_LOPROC  13
#define STT_HIPROC  15


#endif /* _ELF_H*/


/*
 * vim:noexpandtab
 * vim:tabstop=4
 */
