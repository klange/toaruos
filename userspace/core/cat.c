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
				fprintf(stderr, "%s: %s: no such file or directory\n", argv[0], argv[i]);
				ret = 1;
				continue;
			}
		}

		struct stat _stat;
		fstat(fileno(fd), &_stat);

		if (S_ISDIR(_stat.st_mode)) {
			fprintf(stderr, "%s: %s: Is a directory\n", argv[0], argv[i]);
			fclose(fd);
			ret = 1;
			continue;
		}

		while (!feof(fd)) {
			char buf[CHUNK_SIZE];
			int read = fread(buf, 1, CHUNK_SIZE, fd);
			fwrite(buf, 1, read, stdout);
		}
		fflush(stdout);

		fclose(fd);
	}

	return ret;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
