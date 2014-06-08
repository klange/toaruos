/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * compare
 *
 * Compares two files and prints out some
 * statistics on how they differ.
 *
 * This is *NOT* diff.
 */
#include <stdio.h>
#include <string.h>

#define CHUNK_SIZE 1024

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Need two files to compare.\n");
		return 1;
	}

	FILE * a = fopen(argv[1], "r");
	FILE * b = fopen(argv[2], "r");
	size_t lengtha, lengthb;

	fseek(a, 0, SEEK_END);
	lengtha = ftell(a);
	fseek(a, 0, SEEK_SET);

	fseek(b, 0, SEEK_END);
	lengthb = ftell(b);
	fseek(b, 0, SEEK_SET);

	fprintf(stderr,"[%d bytes and %d bytes]\n", lengtha, lengthb);

	char bufa[CHUNK_SIZE];
	char bufb[CHUNK_SIZE];

	int chunk = 0;
	size_t read = 0;

	while (read < lengtha) {
		memset(bufa, 0x0, CHUNK_SIZE);
		memset(bufb, 0x0, CHUNK_SIZE);
		fread(bufa, 1, CHUNK_SIZE, a);
		fread(bufb, 1, CHUNK_SIZE, b);
		size_t different = 0;
		for (int i = 0; i < CHUNK_SIZE; ++i) {
			if (bufa[i] != bufb[i])
				different++;
		}

		if (different > 0) {
			printf("Chunk %d has %d differing bytes.\n", chunk, different);
		}

		read += CHUNK_SIZE;
		chunk++;
	}

	fclose(a);
	fclose(b);


	return 0;
}
