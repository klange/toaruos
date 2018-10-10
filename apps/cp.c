/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 * Copyright (C) 2013 Tyler Bindon
 *
 * cp - Copy files
 *
 * This is an incomplete implementation of `cp`. A more complete
 * version of recursive directory copying can be found in the
 * `migrate` sources, and should probably be imported here.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

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
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], strerror(errno));
		return 1;
	}

	struct stat statbuf;
	stat(argv[1], &statbuf);
	int initial_mode = statbuf.st_mode;
	char * target_path = NULL;

	stat(argv[2], &statbuf);
	if (S_ISDIR(statbuf.st_mode)) {
		char *filename = strrchr(argv[1], '/');
		if (!filename) {
			filename = argv[1];
		}

		target_path = malloc((strlen(argv[2]) + strlen(filename) + 2) * sizeof(char));
		sprintf(target_path, "%s/%s", argv[2], filename );
		fout = fopen( target_path, "w" );

	} else {
		target_path = argv[2];
		fout = fopen( argv[2], "w" );
	}

	if (!fout) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], target_path, strerror(errno));
		return 1;
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

	if (chmod(target_path, initial_mode) < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[2], strerror(errno));
	}

	return 0;
}

