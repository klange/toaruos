/*
 * cat
 */
#include <stdio.h>

#define CHUNK_SIZE 4096

int main(int argc, char ** argv) {
	FILE * fd = stdin;
	if (argc > 1) {
		fd = fopen(argv[1], "r");
		if (!fd) {
			return 1;
		}
	}

	size_t length;

	fseek(fd, 0, SEEK_END);
	length = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	char buf[CHUNK_SIZE];
	while (length > CHUNK_SIZE) {
		fread( buf, 1, CHUNK_SIZE, fd);
		fwrite(buf, 1, CHUNK_SIZE, stdout);
		length -= CHUNK_SIZE;
	}
	if (length > 0) {
		fread( buf, 1, length, fd);
		fwrite(buf, 1, length, stdout);
	}

	fclose(fd);

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
