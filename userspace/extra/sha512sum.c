#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib/sha2.h"

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;
	struct stat buf;
	stat(argv[1], &buf);
	FILE * f = fopen(argv[1], "r");

	char * x = malloc(buf.st_size);
	fread(x, buf.st_size, 1, f);

	/* Generate SHA512 */
	char hash[SHA512_DIGEST_STRING_LENGTH];
	SHA512_Data(x, buf.st_size, hash);
	fprintf(stderr, "%s  %s\n", hash, argv[1]);
	return 0;
}
