/**
 * @file elf.h
 * @author K. Lange
 *
 * @copyright Copyright in this work is disclaimed under the assertion
 * that its contents are purely factual and no rights may be reserved
 * for its use.
 *
 * @brief Structures for Elf binary files.
 *
 * Based primarily on the Elf and SysV ABI specification documents.
 *
 * @see https://uclibc.org/docs/elf-64-gen.pdf
 * @see https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf
 */
#pragma once
#include <stdint.h>

/**
 * @typedef Elf64_Addr
 * @brief Unsigned program address. (uintptr_t)
 * @typedef Elf64_Off
 * @brief Unsigned file offset. (size_t)
 * @typedef Elf64_Half
 * @brief Unsigned medium integer. (unsigned short)
 * @typedef Elf64_Word
 * @brief Unsigned integer. (unsigned int)
 * @typedef Elf64_Sword
 * @brief Signed integer. (int)
 * @typedef Elf64_Xword
 * @brief Unsigned long integer. (unsigned long long)
 * @typedef Elf64_Sxword
 * @brief Signed long integer. (long long)
 */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

/**
 * Values for e_ident[EI_MAGn]
 */
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'

/**
 * Values for e_ident[EI_CLASS]
 */
#define ELFCLASS32 1
#define ELFCLASS64 2

/**
 * Values for e_ident[EI_DATA]
 */
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

/**
 * Values for e_type
 */
#define ET_NONE    0
#define ET_REL     1
#define ET_EXEC    2
#define ET_DYN     3
#define ET_CORE    4

/**
 * e_ident fields
 */
#define EI_MAG0        0
#define EI_MAG1        1
#define EI_MAG2        2
#define EI_MAG3        3
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_OSABI       7
#define EI_ABIVERSION  8
#define EI_PAD         9
#define EI_NIDENT      16

#define EM_X86_64      62

/**
 * @brief Elf object file header.
 */
typedef struct Elf64_Header {
	uint8_t    e_ident[EI_NIDENT]; /**< @brief Identifies the layout of the rest of the file. */
	Elf64_Half e_type;             /**< @brief What kind of file this is, eg. object or executable... */
	Elf64_Half e_machine;          /**< @brief The architecture this file is for. */
	Elf64_Word e_version;          /**< @brief The version of the standard this file confirms to. */
	Elf64_Addr e_entry;            /**< @brief The entry point of an executable. */
	Elf64_Off  e_phoff;            /**< @brief The offset of the program headers. */
	Elf64_Off  e_shoff;            /**< @brief The offset of the section headers. */
	Elf64_Word e_flags;            /**< @brief Various flags. */
	Elf64_Half e_ehsize;           /**< @brief Size of this header. */
	Elf64_Half e_phentsize;        /**< @brief Size of one program header table entry. */
	Elf64_Half e_phnum;            /**< @brief The number of entries in the program header table. */
	Elf64_Half e_shentsize;        /**< @brief Size of one section header table entry. */
	Elf64_Half e_shnum;            /**< @brief The number of entries in the section header table. */
	Elf64_Half e_shstrndx;
} Elf64_Header;

/**
 * Special section indices
 */
#define SHN_UNDEF    0
#define SHN_LOPROC   0xFF00
#define SHN_HIPROC   0xFF1F
#define SHN_LOOS     0xFF20
#define SHN_HIOS     0xFF3F
#define SHN_ABS      0xFFF1
#define SHN_COMMON   0xFFF2

/**
 * Values for sh_type, sh_link, sh_info
 */
#define SHT_NULL          0
#define SHT_PROGBITS      1
#define SHT_SYMTAB        2
#define SHT_STRTAB        3
#define SHT_RELA          4
#define SHT_HASH          5
#define SHT_DYNAMIC       6
#define SHT_NOTE          7
#define SHT_NOBITS        8
#define SHT_REL           9
#define SHT_SHLIB         10
#define SHT_DYNSYM        11
#define SHT_LOOS          0x60000000
#define SHT_HIOS          0x6FFFFFFF
#define SHT_LOPROC        0x70000000
#define SHT_HIPROC        0x7FFFFFFF

/**
 * Values for sh_flags
 */
#define SHF_WRITE         0x00000001
#define SHF_ALLOC         0x00000002
#define SHF_EXECINSTR     0x00000004
#define SHF_MASKOS        0x0F000000
#define SHF_MASKPROC      0xF0000000
/* From the SysV x86-64 ABI */
#define SHF_X86_64_LARGE  0x10000000
#define SHF_X86_64_UNWIND 0x70000001

typedef struct Elf64_Shdr {
	Elf64_Word  sh_name;
	Elf64_Word  sh_type;
	Elf64_Xword sh_flags;
	Elf64_Addr  sh_addr;
	Elf64_Off   sh_offset;
	Elf64_Xword sh_size;
	Elf64_Word  sh_link;
	Elf64_Word  sh_info;
	Elf64_Xword sh_addralign;
	Elf64_Xword sh_entsize;
} Elf64_Shdr;

/**
 * Binding types.
 * Contained in the high four bits of @p st_info
 */
#define STB_LOCAL    0 /**< @brief Not visible outside the object file. */
#define STB_GLOBAL   1 /**< @brief Global symbol, visible to all object files. */
#define STB_WEAK     2 /**< @brief Global scope, but with lower precedence than global symbols. */
#define STB_LOOS    10
#define STB_HIOS    12
#define STB_LOPROC  13
#define STB_HIPROC  15

/**
 * Symbol types.
 * Contained in the low four bits of @p st_info
 */
#define STT_NOTYPE   0 /**< @brief No type specified (e.g., an absolute symbol) */
#define STT_OBJECT   1 /**< @brief Data object */
#define STT_FUNC     2 /**< @brief Function entry point */
#define STT_SECTION  3 /**< @brief Symbol is associated with a section */
#define STT_FILE     4 /**< @brief Source file associated with the object */
#define STT_LOOS    10
#define STT_HIOS    12
#define STT_LOPROC  13
#define STT_HIPROC  15

typedef struct Elf64_Sym {
	Elf64_Word    st_name;  /**< @brief Symbol name */
	unsigned char st_info;  /**< @brief Type and binding attributes */
	unsigned char st_other; /**< @brief Reserved */
	Elf64_Half    st_shndx; /**< @brief Section table index */
	Elf64_Addr    st_value; /**< @brief Symbol value */
	Elf64_Xword   st_size;  /**< @brief Size of object (e.g., common) */
} Elf64_Sym;

/**
 * Relocations
 */

#define ELF64_R_SYM(i)    ((i) >> 32)
#define ELF64_R_TYPE(i)   ((i) & 0xFFFFFFFFL)
#define ELF64_R_INFO(s,t) (((s) << 32) + ((t) & 0xFFFFFFFFL))

typedef struct Elf64_Rel {
	Elf64_Addr  r_offset; /**< @brief Address of reference */
	Elf64_Xword r_info;   /**< @brief Symbol index and type of relocation */
} Elf64_Rel;

typedef struct Elf64_Rela {
	Elf64_Addr   r_offset; /**< @brief Address of reference */
	Elf64_Xword  r_info;   /**< @brief Symbol index and type of relocation */
	Elf64_Sxword r_addend; /**< @brief Constant part of expression */
} Elf64_Rela;

/**
 * x86-64 SysV Relocation types
 */
#define R_X86_64_NONE             0  /**< @brief @p none none */
#define R_X86_64_64               1  /**< @brief @p word64 S + A */
#define R_X86_64_PC32             2  /**< @brief @p word32 S + A - P */
#define R_X86_64_GOT32            3  /**< @brief @p word32 G + A */
#define R_X86_64_PLT32            4  /**< @brief @p word32 L + A - P */
#define R_X86_64_COPY             5  /**< @brief @p none none */
#define R_X86_64_GLOB_DAT         6  /**< @brief @p word64 S */
#define R_X86_64_JUMP_SLOT        7  /**< @brief @p word64 S */
#define R_X86_64_RELATIVE         8  /**< @brief @p word64 B + A */
#define R_X86_64_GOTPCREL         9  /**< @brief @p word32 G + GOT + A - P */
#define R_X86_64_32               10 /**< @brief @p word32 S + A */
#define R_X86_64_32S              11 /**< @brief @p word32 S + A */
/* vvv These should not appear in a valid file */
#define R_X86_64_16               12 /**< @brief @p word16 S + A */
#define R_X86_64_PC16             13 /**< @brief @p word16 S + A - P */
#define R_X86_64_8                14 /**< @brief @p word8  S + A */
#define R_X86_64_PC8              15 /**< @brief @p word8  S + A - P */
/* ^^^ These should not appear in a valid file */
#define R_X86_64_DTPMOD64         16 /**< @brief @p word64 */
#define R_X86_64_DTPOFF64         17 /**< @brief @p word64 */
#define R_X86_64_TPOFF64          18 /**< @brief @p word64 */
#define R_X86_64_TLSGD            19 /**< @brief @p word32 */
#define R_X86_64_TLSLD            20 /**< @brief @p word32 */
#define R_X86_64_DTPOFF32         21 /**< @brief @p word32 */
#define R_X86_64_GOTTPOFF         22 /**< @brief @p word32 */
#define R_X86_64_TPOFF32          23 /**< @brief @p word32 */
#define R_X86_64_PC64             24 /**< @brief @p word64 S + A - P */
#define R_X86_64_GOTOFF64         25 /**< @brief @p word64 S + A - GOT */
#define R_X86_64_GOTPC32          26 /**< @brief @p word32 GOT + A - P */
/* Large model */
#define R_X86_64_GOT64            27 /**< @brief @p word64 G + A */
#define R_X86_64_GOTPCREL64       28 /**< @brief @p word64 G + GOT - P + A */
#define R_X86_64_GOTPC64          29 /**< @brief @p word64 GOT - P + A */
#define R_X86_64_GOTPLT64         30 /**< @brief @p word64 G + A */
#define R_X86_64_PLTOFF64         31 /**< @brief @p word64 L - GOT + A */
/* ... */
#define R_X86_64_SIZE32           32 /**< @brief @p word32 Z + A */
#define R_X86_64_SIZE64           33 /**< @brief @p word64 Z + A */
#define R_X86_64_GOTPC32_TLSDESC  34 /**< @brief @p word32 */
#define R_X86_64_TLSDESC_CALL     35 /**< @brief @p none */
#define R_X86_64_TLSDESC          36 /**< @brief @p word64*2 */
#define R_X86_64_IRELATIVE        37 /**< @brief @p word64 indirect (B + A) */


#define R_AARCH64_COPY          1024
#define R_AARCH64_GLOB_DAT      1025


/**
 * Program header types
 */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6
#define PT_TLS      7
#define PT_LOOS     0x60000000
#define PT_HIOS     0x6FFFFFFF
#define PT_LOPROC   0x70000000
#define PT_HIPROC   0x7FFFFFFF
/* From the SysV x86-64 ABI */
#define PT_GNU_EH_FRAME  0x6474e550
#define PT_SUNW_EH_FRAME 0x6474e550
#define PT_SUNW_UNWIND   0x6464e550


/**
 * Program header flags
 */
#define PF_X        0x01
#define PF_W        0x02
#define PF_R        0x04
#define PF_MASKOS   0x00FF0000
#define PF_MAKSPROC 0xFF000000

typedef struct Elf64_Phdr {
	Elf64_Word  p_type;
	Elf64_Word  p_flags;
	Elf64_Off   p_offset;
	Elf64_Addr  p_vaddr;
	Elf64_Addr  p_paddr;
	Elf64_Xword p_filesz;
	Elf64_Xword p_memsz;
	Elf64_Xword p_align;
} Elf64_Phdr;

/**
 * Dynamic table
 */

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_LOOS   0x60000000
#define DT_HIOS   0x6FFFFFFF
#define DT_LOPROC 0x70000000
#define DT_HIPROC 0x7FFFFFFF

typedef struct Elf64_Dyn {
	Elf64_Sxword d_tag;
	union {
		Elf64_Xword d_val;
		Elf64_Addr  d_ptr;
	} d_un;
} Elf64_Dyn;


