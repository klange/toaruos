/*
 * rm
 *
 * unlink a file
 * (in theory)
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	unlink(argv[1]);

	return 0;
}
