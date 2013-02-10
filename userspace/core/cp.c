/*
 * cp
 */
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
	fout = fopen(argv[2], "w");

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
