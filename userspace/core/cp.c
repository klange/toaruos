/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 * Copyright (C) 2013 Tyler Bindon
 */
/*
 * cp
 */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#define CHUNK_SIZE 4096

int main(int argc, char ** argv) {

	FILE * fd;
	FILE * fout;
	if (argc < 3) {
		fprintf(stderr, "usage: %s [source] [destination]\n", argv[0]);
		return 1;
	}
	fd = fopen(argv[1], "r");
	if (!fd) {
		fprintf(stderr, "%s: %s: no such file or directory\n", argv[0], argv[1]);
		return 1;
	}

	struct stat statbuf;
	stat(argv[2], &statbuf);
	if (S_ISDIR(statbuf.st_mode)) {
		char *filename = strrchr(argv[1], '/');
		if (!filename) {
			filename = argv[1];
		}

		char *target_path = malloc((strlen(argv[2]) + strlen(filename) + 2) * sizeof(char));
		sprintf(target_path, "%s/%s", argv[2], filename );
		fout = fopen( target_path, "w" );

		free(target_path);
	} else {
		fout = fopen( argv[2], "w" );
	}

	size_t length;

	fseek(fd, 0, SEEK_END);
	length = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	char buf[CHUNK_SIZE];
	while (length > CHUNK_SIZE) {
		fread( buf, 1, CHUNK_SIZE, fd);
		fwrite(buf, 1, CHUNK_SIZE, fout);
		length -= CHUNK_SIZE;
	}
	if (length > 0) {
		fread( buf, 1, length, fd);
		fwrite(buf, 1, length, fout);
	}

	fclose(fd);
	fclose(fout);

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
