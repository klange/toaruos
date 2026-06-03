/**
 * @file  nm.c
 * @brief Display symbols from ELF files.
 *
 * Rudimentary implementation based on our readelf.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#define _GNU_SOURCE
#define _TOARU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#ifdef __toaru__
#include <kernel/elf.h>
#else
#include "../base/usr/include/kernel/elf.h"
#endif

static int help(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stdout,
		"%s - display symbols from ELF files\n"
		"\n"
		"usage: %s [<options>] <file>...\n"
		"\n"
		" Supported options:\n"
		"  -A --print-file-name   " X_S "Print the file name on each line." X_E "\n"
		"  -D --dynamic           " X_S "Show dynamic symbols." X_E "\n"
		"  -h --help              " X_S "Show this help text." X_E "\n"
		"  -W --no-weak           " X_S "Skip weak symbols." X_E "\n"
		"  -U --defined-only      " X_S "Show only defined symbols." X_E "\n"
		"  -u --undefined-only    " X_S "Show only undefined symbols." X_E "\n"
		"  -g --extern-only       " X_S "Show only global symbols." X_E "\n"
		"  -p --no-sort           " X_S "Do not sort symbols." X_E "\n"
		"  -r --reverse-sort      " X_S "Reverse the selected sort order." X_E "\n"
		"  -S --print-size        " X_S "Print the sizes of symbols." X_E "\n"
		"  -n --numeric-sort      " X_S "Sort symbols by address." X_E "\n"
		"  -P --portability       " X_S "Use the POSIX output format." X_E "\n"
		"  -j --just-symbols      " X_S "Print only symbol names." X_E "\n"
		"  -t --radix=" X_S "radix       Format numbers (d, o, x)" X_E "\n"
		"     --quiet             " X_S "Do not print message for lack of symbols." X_E "\n"
		"     --size-sort         " X_S "Sort by symbol size and skip undefined." X_E "\n"
		"     --help              " X_S "Show this help text." X_E "\n"
		"\n",
		argv[0], argv[0]);
	return 0;
}

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-hAWuUgDprSnPj] [-t <radix>] <file...>\n", argv[0]);
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
	if (offset >= table->length) return "";
	return table->strings + offset;
}

static char sym_type(Elf64_Sym * sym, Elf64_Shdr *shdr, struct StringTable *sectionStrings) {
	int sb = (sym->st_info >> 4);
	int st = (sym->st_info & 0xF);
	if (sym->st_shndx == SHN_COMMON) return 'C';
	if (sym->st_shndx == SHN_UNDEF) {
		if (sb == STB_WEAK) {
			if (st == STT_OBJECT) return 'v';
			return 'w';
		}
		return 'U';
	}

	if (sb == STB_WEAK) {
		if (st == STT_OBJECT) return 'V';
		return 'W';
	}

	if (sb != STB_GLOBAL && sb != STB_LOCAL) return '?';

	char t = '?';

	if (sym->st_shndx == SHN_ABS) t = 'a';

	if (shdr) {
		if (shdr->sh_type == SHT_NOBITS) t = 'b';
		else if ((shdr->sh_flags & SHF_ALLOC) && (shdr->sh_flags & SHF_EXECINSTR)) t = 't';
		else if ((shdr->sh_flags & SHF_ALLOC) && !(shdr->sh_flags & SHF_WRITE)) t = 'r';
		else if ((shdr->sh_flags & SHF_ALLOC)) t = 'd';
		else {
			const char * name = string_from_table(sectionStrings, shdr->sh_name);
			if (strstr(name,".comment") == name) {
				t = 'N';
			} else if (shdr->sh_size && (shdr->sh_flags & SHF_WRITE) == 0) {
				t = 'n';
			}
		}
	}

	if (sb == STB_GLOBAL) t = toupper(t);

	return t;
}

struct SortContext {
	struct StringTable * strtab;
	int reverse;
};

#if defined(__APPLE__) && (!defined(_POSIX_VERSION) || _POSIX_VERSION < 202405L)
#define sort_args void *c, const void *a, const void *b
#define qsort_r(a,b,c,d,e) qsort_r(a,b,c,e,d)
#else
#define sort_args const void *a, const void *b, void *c
#endif

static int comp_sym(sort_args) {
	struct SortContext * ctx = c;
	const Elf64_Sym * left = ctx->reverse ? b : a;
	const Elf64_Sym * right = ctx->reverse ? a : b;
	return strcmp(string_from_table(ctx->strtab, left->st_name), string_from_table(ctx->strtab, right->st_name));
}

static int comp_size(sort_args) {
	struct SortContext * ctx = c;
	const Elf64_Sym * left = ctx->reverse ? b : a;
	const Elf64_Sym * right = ctx->reverse ? a : b;
	return left->st_size - right->st_size;
}

static int comp_addr(sort_args) {
	struct SortContext * ctx = c;
	const Elf64_Sym * left = ctx->reverse ? b : a;
	const Elf64_Sym * right = ctx->reverse ? a : b;
	return left->st_value - right->st_value;
}

int main(int argc, char * argv[]) {

	static struct option long_opts[] = {
		{"print-file-name", no_argument, 0, 'A'},
		{"dynamic",         no_argument, 0, 'D'},
		{"no-weak",         no_argument, 0, 'W'},
		{"defined-only",    no_argument, 0, 'U'},
		{"undefined-only",  no_argument, 0, 'u'},
		{"extern-only",     no_argument, 0, 'g'},
		{"help",            no_argument, 0, 'h'},
		{"no-sort",         no_argument, 0, 'p'},
		{"reverse-sort",    no_argument, 0, 'r'},
		{"print-size",      no_argument, 0, 'S'},
		{"numeric-sort",    no_argument, 0, 'n'},
		{"portability",     no_argument, 0, 'P'},
		{"just-symbols",    no_argument, 0, 'j'},
		{"radix",           required_argument, 0, 't'},
		{"quiet",           no_argument, 0, 1000},
		{"size-sort",       no_argument, 0, 1001},
		{0,0,0,0}
	};

	int skip_weak = 0;
	int skip_undefined = 0;
	int skip_defined = 0;
	int skip_local = 0;
	int (*sorter)(sort_args) = comp_sym;
	int reverse = 0;
	int quiet = 0;
	int print_size = 0;
	int posix_format = 0;
	int print_file = 0;
	int just_symbols = 0;
	int print_names = 0;
	char radix = 'x';
	Elf64_Word want_section = SHT_SYMTAB;

	int index, c;

	while ((c = getopt_long(argc, argv, "hAWuUgDprSnvPjt:", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'A':
				print_file = 1;
				break;
			case 'W':
				skip_weak = 1;
				break;
			case 'u':
				skip_defined = 1;
				break;
			case 'U':
				skip_undefined = 1;
				break;
			case 'g':
				skip_local = 1;
				break;
			case 'D':
				want_section = SHT_DYNSYM;
				break;
			case 'p':
				sorter = NULL;
				break;
			case 'r':
				reverse = 1;
				break;
			case 'S':
				print_size = 1;
				break;
			case 'n':
			case 'v': /* alias */
				sorter = comp_addr;
				break;
			case 'P':
				posix_format = 1;
				break;
			case 'j':
				just_symbols = 1;
				break;
			case 't':
				radix = *optarg;
				break;
			case 1000:
				quiet = 1;
				break;
			case 1001:
				sorter = comp_size;
				skip_undefined = 1;
				break;
			case 'h':
				return help(argv);
			case '?':
				return usage(argv);
		}
	}

	if (optind >= argc) return usage(argv);
	if (radix != 'd' && radix != 'o' && radix != 'x') return usage(argv);

	int out = 0;

	if (!print_file && !just_symbols && optind + 1 < argc) {
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
			printf("%s%s:\n", posix_format ? "" : "\n", argv[optind]);
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

		if (header.e_ident[EI_CLASS] != ELFCLASS64) {
			fprintf(stderr, "%s: %s: unsupported ELF class\n", argv[0], argv[optind]);
			out = 1;
			continue;
		}

		/* Get the section header string table */
		Elf64_Shdr shstr_hdr;
		fseek(f, header.e_shoff + header.e_shentsize * header.e_shstrndx, SEEK_SET);
		fread(&shstr_hdr, sizeof(Elf64_Shdr), 1, f);
		struct StringTable * stringTable = load_string_table(f, &shstr_hdr);

		int found = 0;

		for (unsigned int i = 0; i < header.e_shnum; ++i) {
			fseek(f, header.e_shoff + header.e_shentsize * i, SEEK_SET);
			Elf64_Shdr sectionHeader;
			fread(&sectionHeader, sizeof(Elf64_Shdr), 1, f);

			if (sectionHeader.sh_size > 0x40000000) continue;
			if (sectionHeader.sh_type != want_section) continue;

			Elf64_Sym * symtab = malloc(sectionHeader.sh_size);
			fseek(f, sectionHeader.sh_offset, SEEK_SET);
			fread(symtab, sectionHeader.sh_size, 1, f);

			Elf64_Shdr shdr_strtab;
			fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_link, SEEK_SET);
			fread(&shdr_strtab, sizeof(Elf64_Shdr), 1, f);
			struct StringTable * strtab = load_string_table(f, &shdr_strtab);

			/* Sort */
			if (sorter) {
				struct SortContext sort_ctx;
				sort_ctx.strtab = strtab;
				sort_ctx.reverse = reverse;
				qsort_r(symtab, sectionHeader.sh_size / sizeof(Elf64_Sym), sizeof(Elf64_Sym), sorter, &sort_ctx);
			}

			for (unsigned int i = 0; i < sectionHeader.sh_size / sizeof(Elf64_Sym); ++i) {
				const char * symname = "";
				int stt = (symtab[i].st_info & 0xF);
				int sb = (symtab[i].st_info >> 4);

				if (stt >= STT_SECTION) continue;
				if (stt == STT_NOTYPE && sb == STB_LOCAL) continue;
				if (skip_weak && sb == STB_WEAK) continue;
				if (skip_local && sb != STB_GLOBAL) continue;
				if (skip_undefined && symtab[i].st_shndx == SHN_UNDEF) continue;
				if (skip_defined && symtab[i].st_shndx != SHN_UNDEF) continue;

				symname = string_from_table(strtab, symtab[i].st_name);
				if (!symname) continue;

				Elf64_Shdr * sym_shdr = NULL;
				Elf64_Shdr shdr_for_sym;

				if (symtab[i].st_shndx > 0 && symtab[i].st_shndx < header.e_shnum) {
					fseek(f, header.e_shoff + header.e_shentsize * symtab[i].st_shndx, SEEK_SET);
					fread(&shdr_for_sym, sizeof(Elf64_Shdr), 1, f);
					sym_shdr = &shdr_for_sym;
				}

				char type = sym_type(&symtab[i], sym_shdr, stringTable);

				if (just_symbols) {
					printf("%s\n", symname);
				} else if (posix_format) {
					if (print_file) printf("%s: ", argv[optind]);
					char fmt[] = " %lx";
					fmt[3] = radix;
					printf("%s %c", symname, type);
					if (symtab[i].st_shndx != SHN_UNDEF) {
						printf(fmt, symtab[i].st_value);
						if (symtab[i].st_size) {
							printf(fmt, symtab[i].st_size);
						}
					}
					printf("\n");
				} else {
					if (print_file) printf("%s:", argv[optind]);
					char fmt[] = "%016lx ";
					fmt[5] = radix;
					if (symtab[i].st_shndx == SHN_UNDEF) {
						printf("                 ");
					} else {
						if (print_size || sorter != comp_size) {
							printf(fmt, symtab[i].st_value);
						}
						if (print_size || sorter == comp_size) {
							printf(fmt, symtab[i].st_size);
						}
					}

					printf("%c %s\n", type, symname);
				}
				found = 1;
			}

			free(strtab);
			free(symtab);
		}

		if (!found) {
			if (!quiet) fprintf(stderr, "%s: %s: no symbols\n", argv[0], argv[optind]);
			out = 1;
		}

		free(stringTable);
	}

	return out;
}

