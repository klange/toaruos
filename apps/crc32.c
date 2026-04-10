/**
 * crc32 - Simple CRC32 calculator for verifying file integrity.
 *
 * Mostly based on code samples from Wikipedia.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>

#define RBUF_SIZE 10240

static char * _argv_0;
static unsigned int crctab[256];
static int reflect_input = 1;
static int reflect_output = 1;
static unsigned int initial_value = 0xFFFFFFFF;
static unsigned int final_xor = 0xFFFFFFFF;
static int print_decimal = 0;
static int extend = 0;

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-d] {-c | -P | [-p polynomial] [-e] [-r] [-R] [-i initial_value] [-x final_xor]} [file...]\n", argv[0]);
	return 1;
}

static unsigned int reflectn(unsigned int from, int n) {
	unsigned int to = 0;
	for (int i = 0; i < n; ++i) {
		to = (to << 1) | (from & 1);
		from >>= 1;
	}
	return to;
}


/**
 * @brief Calculate CRC-32 based on a prebuilt reflected lookup table.
 */
static int calculate(FILE * f, char * name, int print_name) {
	char buf[RBUF_SIZE];
	unsigned long size = 0;
	unsigned int crc32 = initial_value;
	while (!feof(f)) {
		size_t r = fread(buf, 1, RBUF_SIZE, f);
		if (r == 0 && ferror(f)) {
			fprintf(stderr, "%s: %s: %s\n", _argv_0, name, strerror(errno));
			return 1;
		}
		for (size_t i = 0; i < r; ++i) {
			unsigned char b = buf[i];
			if (!reflect_input) b = reflectn(b,8);
			int ind = (crc32 ^ b) & 0xFF;
			crc32 = (crc32 >> 8) ^ (crctab[ind]);
			size += 1;
		}
	}
	if (extend) {
		size_t s = size;
		while (s) {
			unsigned char b = s & 0xFF;
			s >>= 8;
			if (!reflect_input) b = reflectn(b,8);
			int ind = (crc32 ^ b) & 0xFF;
			crc32 = (crc32 >> 8) ^ (crctab[ind]);
		}
	}
	crc32 ^= final_xor;
	if (!reflect_output) crc32 = reflectn(crc32,32);
	if (print_decimal) {
		fprintf(stdout, "%u %lu%s%s\n", crc32, size, (*name) ? " " : "", name);
	} else if (print_name) {
		fprintf(stdout, "%08x\t%s\n", crc32, name);
	} else {
		fprintf(stdout, "%08x\n", crc32);
	}
	return 0;
}

int main(int argc, char * argv[]) {
	_argv_0 = argv[0];
	unsigned int poly = 0x04C11DB7;

	if (!strcmp(basename(argv[0]),"cksum")) {
		reflect_input = 0;
		reflect_output = 0;
		initial_value = 0;
		extend = 1;
		print_decimal = 1;
	}

	int opt;
	while ((opt = getopt(argc, argv, "dei:x:rRcp:P")) != -1) {
		switch (opt) {
			case 'd':
				print_decimal = 1;
				break;
			case 'e':
				extend = 1;
				break;
			case 'i':
				initial_value = strtoul(optarg,NULL,0);
				break;
			case 'x':
				final_xor = strtoul(optarg,NULL,0);
				break;
			case 'r':
				reflect_input = 0;
				break;
			case 'R':
				reflect_output = 0;
				break;
			case 'p':
				poly = strtoul(optarg,NULL,0);
				break;
			case 'c':
				poly = 0x1EDC6F41;
				reflect_input = 1;
				reflect_output = 1;
				initial_value = 0xFFFFFFFF;
				final_xor = 0xFFFFFFFF;
				extend = 0;
				break;
			case 'P':
				poly = 0x04C11DB7;
				reflect_input = 0;
				reflect_output = 0;
				initial_value = 0;
				final_xor = 0xFFFFFFFF;
				extend = 1;
				break;
			case '?':
				return usage(argv);
		}
	}

	/* Bit reverse the polynomial to get the reflected version. */
	unsigned int rpoly = reflectn(poly, 32);

	/*
	 * Calculate reflected lookup table:
	 * @ref https://en.wikipedia.org/wiki/Computation_of_cyclic_redundancy_checks#CRC-32_example
	 */
	unsigned int c = 1;
	for (unsigned int i = 128; i; i >>= 1) {
		c = (c >> 1) ^ (c & 1 ? rpoly : 0);
		for (unsigned int j = 0; j < 256; j += 2 * i) {
			crctab[i + j] = c ^ crctab[j];
		}
	}

	if (optind == argc) {
		return calculate(stdin,"",0);
	}

	int ret = 0;
	int print_names = optind + 1 != argc;

	for (; optind < argc; ++optind) {
		if (!strcmp(argv[optind],"-")) {
			ret |= calculate(stdin,"-", print_names);
		} else {
			FILE * f = fopen(argv[optind], "r");
			if (!f) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
				ret |= 1;
				continue;
			}
			ret |= calculate(f, argv[optind], print_names);
			fclose(f);
		}
	}

	return ret;
}
