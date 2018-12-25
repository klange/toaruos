/* vim: ts=4 sw=4 noexpandtab
 * Really dumb wrapper around gzopen/gzread
 *
 * build with:
 *  i686-pc-toaru-gcc -o base/usr/bin/ungz ungz.c -lz
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>

int main(int argc, char **argv) {
	int ret;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s file.gz\n", argv[0]);
		return 1;
	}

	char * dest_name = NULL;

	if (argc < 3) {
		if (strstr(argv[1],".gz") != (argv[1] + strlen(argv[1]) - 3)) {
			fprintf(stderr, "%s: Not sure if this file is gzipped. Try renaming it to include `.gz' at the end.\n", argv[0]);
			return 1;
		}
		dest_name = strdup(argv[1]);
		char * t = strstr(dest_name,".gz");
		*t = '\0';
	} else {
		dest_name = argv[2];
	}

	gzFile src = gzopen(argv[1], "r");

	if (!src) return 1;

	FILE * dest = fopen(dest_name, "w");

	if (!dest) return 1;

	while (!gzeof(src)) {
		char buf[1024];
		int r = gzread(src, buf, 1024);
		if (r < 0) return 1;
		fwrite(buf, r, 1, dest);
	}

	fclose(dest);

	unlink(argv[1]);

	return 0;
}
