/**
 * @file  readelf.c
 * @brief Display information about a 64-bit Elf binary or object.
 *
 * Implementation of a `readelf` utility.
 *
 * I've tried to get the output here as close to the binutils format
 * as possible, so it should be compatible with tools that try to
 * parse that. Most of the same arguments should also be supported.
 *
 * This is a rewrite of an earlier version.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020-2021 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#ifdef __toaru__
#include <kernel/elf.h>
#else
#include "../base/usr/include/kernel/elf.h"
#endif

#define SHOW_FILE_HEADER      0x0001
#define SHOW_SECTION_HEADERS  0x0002
#define SHOW_PROGRAM_HEADERS  0x0004
#define SHOW_SYMBOLS          0x0008
#define SHOW_DYNAMIC          0x0010
#define SHOW_RELOCATIONS      0x0020

static const char * elf_classToStr(unsigned char ei_class) {
	static char buf[64];
	switch (ei_class) {
		case ELFCLASS32: return "ELF32";
		case ELFCLASS64: return "ELF64";
		default:
			sprintf(buf, "unknown (%d)", ei_class);
			return buf;
	}
}

static const char * elf_dataToStr(unsigned char ei_data) {
	static char buf[64];
	switch (ei_data) {
		case ELFDATA2LSB: return "2's complement, little endian";
		case ELFDATA2MSB: return "2's complement, big endian";
		default:
			sprintf(buf, "unknown (%d)", ei_data);
			return buf;
	}
}

static char * elf_versionToStr(unsigned char ei_version) {
	static char buf[64];
	switch (ei_version) {
		case 1: return "1 (current)";
		default:
			sprintf(buf, "unknown (%d)", ei_version);
			return buf;
	}
}

static char * elf_osabiToStr(unsigned char ei_osabi) {
	static char buf[64];
	switch (ei_osabi) {
		case 0: return "UNIX - System V";
		case 1: return "HP-UX";
		case 255: return "Standalone";
		default:
			sprintf(buf, "unknown (%d)", ei_osabi);
			return buf;
	}
}

static char * elf_typeToStr(Elf64_Half type) {
	static char buf[64];
	switch (type) {
		case ET_NONE: return "NONE (No file type)";
		case ET_REL:  return "REL (Relocatable object file)";
		case ET_EXEC: return "EXEC (Executable file)";
		case ET_DYN:  return "DYN (Shared object file)";
		case ET_CORE: return "CORE (Core file)";
		default:
			sprintf(buf, "unknown (%d)", type);
			return buf;
	}
}

static char * elf_machineToStr(Elf64_Half machine) {
	static char buf[64];
	switch (machine) {
		case EM_X86_64: return "Advanced Micro Devices X86-64";
		default:
			sprintf(buf, "unknown (%d)", machine);
			return buf;
	}
}

static char * sectionHeaderTypeToStr(Elf64_Word type) {
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
			sprintf(buf, "(%x)", type);
			return buf;
	}
}

static char * programHeaderTypeToStr(Elf64_Word type) {
	static char buf[64];
	switch (type) {
		case PT_NULL: return "NULL";
		case PT_LOAD: return "LOAD";
		case PT_DYNAMIC: return "DYNAMIC";
		case PT_INTERP: return "INTERP";
		case PT_NOTE: return "NOTE";
		case PT_PHDR: return "PHDR";
		case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";

		case 0x6474e553: return "GNU_PROPERTY";
		case 0x6474e551: return "GNU_STACK";
		case 0x6474e552: return "GNU_RELRO";

		default:
			sprintf(buf, "(%x)", type);
			return buf;
	}
}

static char * programHeaderFlagsToStr(Elf64_Word flags) {
	static char buf[10];

	snprintf(buf, 10, "%c%c%c   ",
		(flags & PF_R) ? 'R' : ' ',
		(flags & PF_W) ? 'W' : ' ',
		(flags & PF_X) ? 'E' : ' '); /* yes, E, not X... */

	return buf;
}

static char * dynamicTagToStr(Elf64_Dyn * dynEntry, char * dynstr) {
	static char buf[1024];
	static char extra[500];
	char * name = NULL;
	sprintf(extra, "0x%lx", dynEntry->d_un.d_val);

	switch (dynEntry->d_tag) {
		case DT_NULL:
			name = "(NULL)";
			break;
		case DT_NEEDED:
			name = "(NEEDED)";
			sprintf(extra, "[shared lib = %s]", dynstr + dynEntry->d_un.d_val);
			break;
		case DT_PLTRELSZ:
			name = "(PLTRELSZ)";
			break;
		case DT_PLTGOT:
			name = "(PLTGOT)";
			break;
		case DT_HASH:
			name = "(HASH)";
			break;
		case DT_STRTAB:
			name = "(STRTAB)";
			break;
		case DT_SYMTAB:
			name = "(SYMTAB)";
			break;
		case DT_RELA:
			name = "(RELA)";
			break;
		case DT_RELASZ:
			name = "(RELASZ)";
			break;
		case DT_RELAENT:
			name = "(RELAENT)";
			break;
		case DT_STRSZ:
			name = "(STRSZ)";
			sprintf(extra, "%ld (bytes)", dynEntry->d_un.d_val);
			break;
		case DT_SYMENT:
			name = "(SYMENT)";
			sprintf(extra, "%ld (bytes)", dynEntry->d_un.d_val);
			break;
		case DT_INIT:
			name = "(INIT)";
			break;
		case DT_FINI:
			name = "(FINI)";
			break;
		case DT_SONAME:
			name = "(SONAME)";
			break;
		case DT_RPATH:
			name = "(RPATH)";
			break;
		case DT_SYMBOLIC:
			name = "(SYMBOLIC)";
			break;
		case DT_REL:
			name = "(REL)";
			break;
		case DT_RELSZ:
			name = "(RELSZ)";
			sprintf(extra, "%ld (bytes)", dynEntry->d_un.d_val);
			break;
		case DT_RELENT:
			name = "(RELENT)";
			break;
		case DT_PLTREL:
			name = "(PLTREL)";
			sprintf(extra, "%s",
				dynEntry->d_un.d_val == DT_REL ? "REL" : "RELA");
			break;
		case DT_DEBUG:
			name = "(DEBUG)";
			break;
		case DT_TEXTREL:
			name = "(TEXTREL)";
			break;
		case DT_JMPREL:
			name = "(JMPREL)";
			break;
		case DT_BIND_NOW:
			name = "(BIND_NOW)";
			break;
		case DT_INIT_ARRAY:
			name = "(INIT_ARRAY)";
			break;
		case DT_FINI_ARRAY:
			name = "(FINI_ARRAY)";
			break;
		case DT_INIT_ARRAYSZ:
			name = "(INIT_ARRAYSZ)";
			sprintf(extra, "%ld (bytes)", dynEntry->d_un.d_val);
			break;
		case DT_FINI_ARRAYSZ:
			name = "(FINI_ARRASZ)";
			sprintf(extra, "%ld (bytes)", dynEntry->d_un.d_val);
			break;
		case 0x1E:
			name = "(FLAGS)";
			break;
		case 0x6ffffef5:
			name = "(GNU_HASH)";
			break;
		case 0x6ffffffb:
			name = "(FLAGS_1)";
			break;
		case 0x6ffffffe:
			name = "(VERNEED)";
			break;
		case 0x6fffffff:
			name = "(VERNEEDNUM)";
			sprintf(extra, "%ld", dynEntry->d_un.d_val);
			break;
		case 0x6ffffff0:
			name = "(VERSYM)";
			break;
		case 0x6ffffff9:
			name = "(RELACOUNT)";
			snprintf(extra, 500, "%ld", dynEntry->d_un.d_val);
			break;
		default:
			name = "(unknown)";
			break;
	}

	snprintf(buf, 1024, "%-15s %s", name, extra);
	return buf;
}

static char * relocationInfoToStr(Elf64_Xword info) {
#define CASE(o) case o: return #o;
	switch (info) {
		CASE(R_X86_64_NONE)
		CASE(R_X86_64_64)
		CASE(R_X86_64_PC32)
		CASE(R_X86_64_GOT32)
		CASE(R_X86_64_PLT32)
		CASE(R_X86_64_COPY)
		CASE(R_X86_64_GLOB_DAT)
		CASE(R_X86_64_JUMP_SLOT)
		CASE(R_X86_64_RELATIVE)
		CASE(R_X86_64_GOTPCREL)
		CASE(R_X86_64_32)
		CASE(R_X86_64_32S)
		CASE(R_X86_64_DTPMOD64)
		CASE(R_X86_64_DTPOFF64)
		CASE(R_X86_64_TPOFF64)
		CASE(R_X86_64_TLSGD)
		CASE(R_X86_64_TLSLD)
		CASE(R_X86_64_DTPOFF32)
		CASE(R_X86_64_GOTTPOFF)
		CASE(R_X86_64_TPOFF32)
		CASE(R_X86_64_PC64)
		CASE(R_X86_64_GOTOFF64)
		CASE(R_X86_64_GOTPC32)
		CASE(R_X86_64_GOT64)
		CASE(R_X86_64_GOTPCREL64)
		CASE(R_X86_64_GOTPC64)
		CASE(R_X86_64_GOTPLT64)
		CASE(R_X86_64_PLTOFF64)
		CASE(R_X86_64_SIZE32)
		CASE(R_X86_64_SIZE64)
		CASE(R_X86_64_GOTPC32_TLSDESC)
		CASE(R_X86_64_TLSDESC_CALL)
		CASE(R_X86_64_TLSDESC)
		CASE(R_X86_64_IRELATIVE)
		default:
			return "unknown";
	}
#undef CASE
}

static char * symbolTypeToStr(int type) {
	static char buf[10];
	switch (type) {
		case STT_NOTYPE:  return "NOTYPE";
		case STT_OBJECT:  return "OBJECT";
		case STT_FUNC:    return "FUNC";
		case STT_SECTION: return "SECTION";
		case STT_FILE:    return "FILE";
		default:
			sprintf(buf, "%x", type);
			return buf;
	}
}

static char * symbolBindToStr(int bind) {
	static char buf[10];
	switch (bind) {
		case STB_LOCAL:  return "LOCAL";
		case STB_GLOBAL: return "GLOBAL";
		case STB_WEAK:   return "WEAK";
		default:
			sprintf(buf, "%x", bind);
			return buf;
	}
}

static char * symbolVisToStr(int vis) {
	static char buf[10];
	switch (vis) {
		case 0: return "DEFAULT";
		case 1: return "INTERNAL";
		case 2: return "HIDDEN";
		case 3: return "PROTECTED";
		default:
			sprintf(buf, "%x", vis);
			return buf;
	}
}

static char * symbolNdxToStr(int ndx) {
	static char buf[10];
	switch (ndx) {
		case 0:     return "UND";
		case 65521: return "ABS";
		default:
			sprintf(buf, "%d", ndx);
			return buf;
	}

}

static int usage(char * argv[]) {
	fprintf(stderr,
		"Usage: %s <option(s)> elf-file(s)\n"
		" Displays information about ELF object files in a GNU binutils-compatible way.\n"
		" Supported options:\n"
		"  -a --all             Equivalent to -h -l -S -s -d -r\n"
		"  -h --file-header     Display the ELF file header\n"
		"  -l --program-headers Display the program headers\n"
		"  -S --section-headers Display the section headers\n"
		"  -e --headers         Equivalent to -h -l -S\n"
		"  -s --syms            Display symbol table\n"
		"  -d --dynamic         Display dynamic section\n"
		"  -r --relocs          Display relocations\n"
		"  -H --help            Show this help text\n"
		" Aliases:\n"
		"  --segments   Same as --file-header\n"
		"  --sections   Same as --section-headers\n"
		"  --symbols    Same as --syms\n"
		, argv[0]);
	return 1;
}

struct StringTable {
	size_t length;
	char strings[];
};

static struct StringTable * load_string_table(FILE * f, Elf64_Shdr * header) {
	struct StringTable * out = malloc(sizeof(struct StringTable) + header->sh_size + 1);
	out->length = header->sh_size;
	fseek(f, header->sh_offset, SEEK_SET);
	fread(out->strings, header->sh_size, 1, f);
	out->strings[out->length] = 0;
	return out;
}

static const char * string_from_table(struct StringTable * table, size_t offset) {
	if (offset >= table->length) return "(out of range)";
	return table->strings + offset;
}

int main(int argc, char * argv[]) {

	static struct option long_opts[] = {
		{"all",             no_argument, 0, 'a'},
		{"file-header",     no_argument, 0, 'h'},
		{"program-headers", no_argument, 0, 'l'},
		{"section-headers", no_argument, 0, 'S'},
		{"headers",         no_argument, 0, 'e'},
		{"syms",            no_argument, 0, 's'},
		{"dynamic",         no_argument, 0, 'd'},
		{"relocs",          no_argument, 0, 'r'},
		{"help",            no_argument, 0, 'H'},

		{"segments",        no_argument, 0, 'l'}, /* Alias for --program-headers */
		{"sections",        no_argument, 0, 'S'}, /* Alias for --section-headers */
		{"symbols",         no_argument, 0, 's'}, /* Alias for --syms */
		{0,0,0,0}
	};

	int show_bits = 0;
	int index, c;

	while ((c = getopt_long(argc, argv, "ahlSesdrH", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'H':
				return usage(argv);
			case 'a':
				show_bits |= SHOW_FILE_HEADER | SHOW_SECTION_HEADERS | SHOW_PROGRAM_HEADERS | SHOW_SYMBOLS | SHOW_DYNAMIC | SHOW_RELOCATIONS;
				break;
			case 'd':
				show_bits |= SHOW_DYNAMIC;
				break;
			case 'h':
				show_bits |= SHOW_FILE_HEADER;
				break;
			case 'l':
				show_bits |= SHOW_PROGRAM_HEADERS;
				break;
			case 'S':
				show_bits |= SHOW_SECTION_HEADERS;
				break;
			case 'e':
				show_bits |= SHOW_FILE_HEADER | SHOW_PROGRAM_HEADERS | SHOW_SECTION_HEADERS;
				break;
			case 's':
				show_bits |= SHOW_SYMBOLS;
				break;
			case 'r':
				show_bits |= SHOW_RELOCATIONS;
				break;
			default:
				fprintf(stderr, "Unrecognized option: %c\n", c);
				break;
		}
	}

	if (optind >= argc || !show_bits) {
		return usage(argv);
	}

	int out = 0;
	int print_names = 0;

	if (optind + 1 < argc) {
		print_names = 1;
	}

	for (; optind < argc; optind++) {
		FILE * f = fopen(argv[optind],"r");

		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			out = 1;
			continue;
		}

		if (print_names) {
			printf("\nFile: %s\n", argv[optind]);
		}

		/**
		 * Validate header.
		 */
		Elf64_Header header;
		fread(&header, sizeof(Elf64_Header), 1, f);

		if (memcmp("\x7F" "ELF",&header,4)) {
			fprintf(stderr, "%s: %s: not an elf\n", argv[0], argv[optind]);
			out = 1;
			continue;
		}

		if (show_bits & SHOW_FILE_HEADER) {
			printf("ELF Header:\n");
			printf("  Magic:   %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				header.e_ident[0],  header.e_ident[1],  header.e_ident[2],  header.e_ident[3],
				header.e_ident[4],  header.e_ident[5],  header.e_ident[6],  header.e_ident[7],
				header.e_ident[8],  header.e_ident[9],  header.e_ident[10], header.e_ident[11],
				header.e_ident[12], header.e_ident[13], header.e_ident[14], header.e_ident[15]);
			printf("  Class:                             %s\n", elf_classToStr(header.e_ident[EI_CLASS]));
			printf("  Data:                              %s\n", elf_dataToStr(header.e_ident[EI_DATA]));
			printf("  Version:                           %s\n", elf_versionToStr(header.e_ident[EI_VERSION]));
			printf("  OS/ABI:                            %s\n", elf_osabiToStr(header.e_ident[EI_OSABI]));
			printf("  ABI Version:                       %u\n", header.e_ident[EI_ABIVERSION]);
		}

		if (header.e_ident[EI_CLASS] != ELFCLASS64) {
			continue;
		}

		if (show_bits & SHOW_FILE_HEADER) {
			/* Byte-order dependent from here on out... */
			printf("  Type:                              %s\n", elf_typeToStr(header.e_type));
			printf("  Machine:                           %s\n", elf_machineToStr(header.e_machine));
			printf("  Version:                           0x%x\n", header.e_version);
			printf("  Entry point address:               0x%lx\n", header.e_entry);
			printf("  Start of program headers:          %lu (bytes into file)\n", header.e_phoff);
			printf("  Start of section headers:          %lu (bytes into file)\n", header.e_shoff);
			printf("  Flags:                             0x%x\n", header.e_flags);
			printf("  Size of this header:               %u (bytes)\n", header.e_ehsize);
			printf("  Size of program headers:           %u (bytes)\n", header.e_phentsize);
			printf("  Number of program headers:         %u\n", header.e_phnum);
			printf("  Size of section headers:           %u (bytes)\n", header.e_shentsize);
			printf("  Number of section headers:         %u\n", header.e_shnum);
			printf("  Section header string table index: %u\n", header.e_shstrndx);
		}

		/* Get the section header string table */
		Elf64_Shdr shstr_hdr;
		fseek(f, header.e_shoff + header.e_shentsize * header.e_shstrndx, SEEK_SET);
		fread(&shstr_hdr, sizeof(Elf64_Shdr), 1, f);

		struct StringTable * stringTable = load_string_table(f, &shstr_hdr);

		/**
		 * Section Headers
		 */
		if (show_bits & SHOW_SECTION_HEADERS) {
			printf("\nSection Headers:\n");
			printf("  [Nr] Name              Type             Address           Offset\n");
			printf("       Size              EntSize          Flags  Link  Info  Align\n");
			for (unsigned int i = 0; i < header.e_shnum; ++i) {
				fseek(f, header.e_shoff + header.e_shentsize * i, SEEK_SET);
				Elf64_Shdr sectionHeader;
				fread(&sectionHeader, sizeof(Elf64_Shdr), 1, f);

				printf("  [%2d] %-17.17s %-16.16s %016lx  %08lx\n",
					i, string_from_table(stringTable, sectionHeader.sh_name), sectionHeaderTypeToStr(sectionHeader.sh_type),
					sectionHeader.sh_addr, sectionHeader.sh_offset);
				printf("       %016lx  %016lx %4ld %6d %5d %5ld\n",
					sectionHeader.sh_size, sectionHeader.sh_entsize, sectionHeader.sh_flags,
					sectionHeader.sh_link, sectionHeader.sh_info, sectionHeader.sh_addralign);
			}
		}

		/**
		 * Program Headers
		 */
		if (show_bits & SHOW_PROGRAM_HEADERS && header.e_phoff && header.e_phnum) {
			printf("\nProgram Headers:\n");
			printf("  Type           Offset             VirtAddr           PhysAddr\n");
			printf("                 FileSiz            MemSiz              Flags  Align\n");
			for (unsigned int i = 0; i < header.e_phnum; ++i) {
				fseek(f, header.e_phoff + header.e_phentsize * i, SEEK_SET);
				Elf64_Phdr programHeader;
				fread(&programHeader, sizeof(Elf64_Phdr), 1, f);

				printf("  %-14.14s 0x%016lx 0x%016lx 0x%016lx\n",
					programHeaderTypeToStr(programHeader.p_type),
					programHeader.p_offset, programHeader.p_vaddr, programHeader.p_paddr);
				printf("                 0x%016lx 0x%016lx  %s 0x%lx\n",
					programHeader.p_filesz, programHeader.p_memsz,
					programHeaderFlagsToStr(programHeader.p_flags), programHeader.p_align);

				if (programHeader.p_type == PT_INTERP) {
					/* Read interpreter string */
					char * tmp = malloc(programHeader.p_filesz);
					fseek(f, programHeader.p_offset, SEEK_SET);
					fread(tmp, programHeader.p_filesz, 1, f);
					printf("    [Requesting program interpreter: %.*s]\n",
						(int)programHeader.p_filesz, tmp);
					free(tmp);
				}
			}
		}

		/* TODO Section to segment mapping? */

		/**
		 * Dump section information.
		 */
		for (unsigned int i = 0; i < header.e_shnum; ++i) {
			fseek(f, header.e_shoff + header.e_shentsize * i, SEEK_SET);
			Elf64_Shdr sectionHeader;
			fread(&sectionHeader, sizeof(Elf64_Shdr), 1, f);

			if (sectionHeader.sh_size > 0x40000000) {
				/* Suspiciously large section header... */
				continue;
			}

			/* I think there should only be one of these... */
			switch (sectionHeader.sh_type) {
				case SHT_DYNAMIC:
					if (show_bits & SHOW_DYNAMIC) {
						printf("\nDynamic section at offset 0x%lx contains (up to) %ld entries:\n",
							sectionHeader.sh_offset, sectionHeader.sh_size / sectionHeader.sh_entsize);
						printf("  Tag        Type                         Name/Value\n");

						/* Read the linked string table */
						Elf64_Shdr dynstr;
						fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_link, SEEK_SET);
						fread(&dynstr, sizeof(Elf64_Shdr), 1, f);
						char * dynStr = malloc(dynstr.sh_size);
						fseek(f, dynstr.sh_offset, SEEK_SET);
						fread(dynStr, dynstr.sh_size, 1, f);

						char * dynTable = malloc(sectionHeader.sh_size);
						fseek(f, sectionHeader.sh_offset, SEEK_SET);
						fread(dynTable, sectionHeader.sh_size, 1, f);

						for (unsigned int i = 0; i < sectionHeader.sh_size / sectionHeader.sh_entsize; i++) {
							Elf64_Dyn * dynEntry = (Elf64_Dyn *)(dynTable + sectionHeader.sh_entsize * i);

							printf(" 0x%016lx %s\n",
								dynEntry->d_tag,
								dynamicTagToStr(dynEntry, dynStr));

							if (dynEntry->d_tag == DT_NULL) break;
						}

						free(dynStr);
						free(dynTable);
					}
					break;
				case SHT_RELA:
					if (show_bits & SHOW_RELOCATIONS) {
						printf("\nRelocation section '%s' at offset 0x%lx contains %ld entries.\n",
							string_from_table(stringTable, sectionHeader.sh_name), sectionHeader.sh_offset,
							sectionHeader.sh_size / sizeof(Elf64_Rela));
						printf("  Offset          Info           Type           Sym. Value    Sym. Name + Addend\n");

						/* Section this relocation is in */
						Elf64_Shdr shdr_this;
						fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_info, SEEK_SET);
						fread(&shdr_this, sizeof(Elf64_Shdr), 1, f);

						/* Symbol table link */
						Elf64_Shdr shdr_symtab;
						fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_link, SEEK_SET);
						fread(&shdr_symtab, sizeof(Elf64_Shdr), 1, f);
						Elf64_Sym * symtab = malloc(shdr_symtab.sh_size);
						fseek(f, shdr_symtab.sh_offset, SEEK_SET);
						fread(symtab, shdr_symtab.sh_size, 1, f);

						/* Symbol table's string table link */
						Elf64_Shdr shdr_strtab;
						fseek(f, header.e_shoff + header.e_shentsize * shdr_symtab.sh_link, SEEK_SET);
						fread(&shdr_strtab, sizeof(Elf64_Shdr), 1, f);
						struct StringTable * strtab = load_string_table(f, &shdr_strtab);

						/* Load relocations from file */
						Elf64_Rela * relocations = malloc(sectionHeader.sh_size);
						fseek(f, sectionHeader.sh_offset, SEEK_SET);
						fread((void*)relocations, sectionHeader.sh_size, 1, f);

						for (unsigned int i = 0; i < sectionHeader.sh_size / sizeof(Elf64_Rela); ++i) {
							Elf64_Shdr shdr;
							size_t offset = ELF64_R_SYM(relocations[i].r_info);
							Elf64_Xword value = 42;
							printf("%012lx  %012lx %-15.15s ",
								relocations[i].r_offset, relocations[i].r_info,
								relocationInfoToStr(ELF64_R_TYPE(relocations[i].r_info)));
							const char * symName = "(null)";
							if (!offset) {
								printf("                ");
							} else if (offset < shdr_symtab.sh_size) {
								Elf64_Sym * this = &symtab[offset];

								/* Get symbol name for this relocation */
								if ((this->st_info & 0xF) == STT_SECTION) {
									fseek(f, header.e_shoff + header.e_shentsize * this->st_shndx, SEEK_SET);
									fread(&shdr, sizeof(Elf64_Shdr), 1, f);
									symName = string_from_table(stringTable, shdr.sh_name);
								} else {
									symName = string_from_table(strtab, this->st_name);
								}

								value = this->st_value + relocations[i].r_addend;
								printf("%016lx %s +", value, symName);
							}
							printf(" %lx\n", relocations[i].r_addend);
						}

						free(relocations);
						free(strtab);
						free(symtab);
					}
					break;
				case SHT_DYNSYM:
				case SHT_SYMTAB:
					if (show_bits & SHOW_SYMBOLS) {
						printf("\nSymbol table '%s' contains %ld entries.\n",
							string_from_table(stringTable, sectionHeader.sh_name),
							sectionHeader.sh_size / sizeof(Elf64_Sym));
						printf("   Num:    Value          Size Type    Bind   Vis      Ndx Name\n");

						Elf64_Sym * symtab = malloc(sectionHeader.sh_size);
						fseek(f, sectionHeader.sh_offset, SEEK_SET);
						fread(symtab, sectionHeader.sh_size, 1, f);

						Elf64_Shdr shdr_strtab;
						fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_link, SEEK_SET);
						fread(&shdr_strtab, sizeof(Elf64_Shdr), 1, f);
						struct StringTable * strtab = load_string_table(f, &shdr_strtab);

						for (unsigned int i = 0; i < sectionHeader.sh_size / sizeof(Elf64_Sym); ++i) {
							printf("%6u: %016lx %6lu %-7.7s %-6.6s %-7.7s %4s %s\n",
								i, symtab[i].st_value, symtab[i].st_size,
								symbolTypeToStr(symtab[i].st_info & 0xF),
								symbolBindToStr(symtab[i].st_info >> 4),
								symbolVisToStr(symtab[i].st_other),
								symbolNdxToStr(symtab[i].st_shndx),
								string_from_table(strtab, symtab[i].st_name));
						}

						free(strtab);
						free(symtab);
					}
					break;
				default:
					break;

			}
		}

		free(stringTable);
	}

	return out;
}
