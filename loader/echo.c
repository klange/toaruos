#include <syscall.h>

/* I really need a standard library */
int
strlen(
		const char *str
	  ) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

void usage() {
	char * str ="echo [-n] [-e] [STRING]...\n"
				"  -n    do not output a new line at the end\n"
				"  -e    process escape sequences\n";
	syscall_write(1, str, strlen(str));
}

int main(int argc, char ** argv) {
	int start           = 1;
	int use_newline     = 1;
	int process_escapes = 0;

	for (int i = start; i < argc; ++i) {
		if (argv[i][0] != '-') {
			start = i;
			break;
		} else {
			if (argv[i][1] == 'h') {
				usage();
				return 1;
			} else if (argv[i][1] == 'n') {
				use_newline = 0;
			} else if (argv[i][1] == 'e') {
				process_escapes = 1;
			}
		}
	}

	for (int i = start; i < argc; ++i) {
		if (process_escapes) {
			for (int j = 0; j < strlen(argv[i]) - 1; ++j) {
				if (argv[i][j] == '\\') {
					if (argv[i][j+1] == 'e') {
						argv[i][j] = '\033';
						for (int k = j + 1; k < strlen(argv[i]); ++k) {
							argv[i][k] = argv[i][k+1];
						}
					}
					if (argv[i][j+1] == 'n') {
						argv[i][j] = '\n';
						for (int k = j + 1; k < strlen(argv[i]); ++k) {
							argv[i][k] = argv[i][k+1];
						}
					}
				}
			}
		}
		syscall_write(1, argv[i], strlen(argv[1]));
		if (i != argc - 1) {
			syscall_write(1, " ", 1);
		}
	}

	if (use_newline) {
		syscall_write(1, "\n", 1);
	}
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
