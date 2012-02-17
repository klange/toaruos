/*
 * cat
 */
#include <stdio.h>

#define CHUNK_SIZE 4096

int main(int argc, char ** argv) {
	int ret = 0;

	for (int i = 1; i < argc; ++i) {
		FILE * fd;
		if (argc > 1) {
			fd = fopen(argv[i], "r");
			if (!fd) {
				fprintf(stderr, "%s: %s: no such file or directory\n", argv[0], argv[1]);
				ret = 1;
				continue;
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
			fflush(stdout);
			length -= CHUNK_SIZE;
		}
		if (length > 0) {
			fread( buf, 1, length, fd);
			fwrite(buf, 1, length, stdout);
			fflush(stdout);
		}

		fclose(fd);
	}

	return ret;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
