/*
 * cat
 *
 * Concatenates files together to standard output.
 * In a supporting terminal, you can then pipe
 * standard out to another file or other useful
 * things like that.
 */
#include <stdio.h>
#include <sys/stat.h>

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

		struct stat _stat;
		fstat(fileno(fd), &_stat);

		if (S_ISCHR(_stat.st_mode)) {
			/* character devices should be read byte by byte until we get a 0 respones */

			while (1) {
				char buf[2];
				size_t read = fread(buf, 1, 1, fd);
				if (!read) {
					break;
				}
				fwrite(buf, 1, read, stdout);
				fflush(stdout);
			}

		} else {
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
