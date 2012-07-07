/*
 * edit
 *
 * A super-simple one-pass file... uh "editor".
 * Takes stdin until a blank line and writes
 * it back to standard out.
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	FILE * f = fopen(argv[1], "w");

	if (!f) {
		fprintf(stderr, "%s: Could not open %s\n", argv[0], argv[1]);
		return 1;
	}

	while (1) {
		char buf[1024];
		fgets(buf, 1024, stdin);
		buf[strlen(buf)-1] = '\0';

		if (strlen(buf) < 2) {
			break;
		}
		fprintf(f, "%s\n", buf);
	}

	fclose(f);


	return 0;
}
