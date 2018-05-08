#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char * argv[]) {
	char * term = getenv("TERM");
	if (!term || strstr(term, "toaru") != term) {
		fprintf(stderr, "Unrecognized terminal. These commands are for the とある terminal only.\n");
		return 1;
	}
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	if (!strcmp(argv[1], "alpha")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [0 or 1]\n", argv[0], argv[1]);
			return 1;
		}
		int i = atoi(argv[2]);
		if (i) {
			printf("\033[2001z");
		} else {
			printf("\033[2000z");
		}
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "scale")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [floating point size, 1.0 = normal]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[1555;%sz", argv[2]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "gamma")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [floating point gamma, 1.7 = normal]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[1556;%sz", argv[2]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "sdf")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [sdf enabled, 1 = yes]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[1557;%sz", argv[2]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "size")) {
		if (argc < 4) {
			fprintf(stderr, "%s %s [width] [height]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[3000;%s;%sz", argv[2], argv[3]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "--help")) {
		fprintf(stderr, "Available arguments:\n"
		                "  alpha - alpha transparency enabled / disabled\n"
		                "  scale - font scaling\n"
		                "  size - terminal width/height in characters\n"
		                "  force-raw - sets terminal to raw mode before commands\n"
		                "  no-force-raw - disables forced raw mode\n"
		);
		return 0;
	}

	fprintf(stderr, "%s: unrecognized argument\n", argv[0]);
	return 1;
}
