/**
 * @brief chmod - change file permissions
 *
 * This implementation is likely non-compliant, though it does
 * attempt to look similar to the standard POSIX syntax,
 * supporting both octal mode setings and +/-rwx flavors.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <toaru/modecalc.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-R] mode file...\n", argv[0]);
	return 1;
}

static void describe_mode(char *modestr, mode_t mode) {
	char * c = modestr;
	*c++ = (mode & S_IRUSR) ? 'r' : '-';
	*c++ = (mode & S_IWUSR) ? 'w' : '-';
	*c++ = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');

	*c++ = (mode & S_IRGRP) ? 'r' : '-';
	*c++ = (mode & S_IWGRP) ? 'w' : '-';
	*c++ = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');

	*c++ = (mode & S_IROTH) ? 'r' : '-';
	*c++ = (mode & S_IWOTH) ? 'w' : '-';
	*c++ = (mode & S_IXOTH) ? 'x' : '-';
	*c = '\0';
}

int main(int argc, char * argv[]) {
	int opt;
	int recursive = 0;
	int verbose = 0;

	while ((opt = getopt(argc, argv, "Rv")) != -1) {
		switch (opt) {
			case 'R':
				fprintf(stderr, "%s: warning: recursion unsupported; each directory will yield a further diagnostic\n", argv[0]);
				recursive = 1;
				break;
			case 'v':
				verbose = 1;
				break;
			default:
				return usage(argv);
		}
	}

	if (argc < optind + 1) return usage(argv);

	if (mode_calc(argv[optind], 0, 0, 0) == (mode_t)-1) {
		fprintf(stderr, "%s: unsupported mode '%s'\n", argv[0], argv[optind]);
		return 2;
	}

	mode_t mask = umask(0);
	umask(mask);

	int out = 0;
	for (int i = optind + 1; i < argc; ++i) {
		struct stat _stat;
		if (stat(argv[i], &_stat) < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			out |= 1;
			continue;
		}

		if (recursive && S_ISDIR(_stat.st_mode)) {
			fprintf(stderr, "%s: %s: recursion ignored on directory\n", argv[0], argv[i]);
		}

		mode_t old_mode = _stat.st_mode & 07777;
		mode_t new_mode = mode_calc(argv[optind], old_mode, mask, S_ISDIR(_stat.st_mode) ? 2 : 0);
		char old_mode_desc[10];
		char new_mode_desc[10];

		if (verbose) {
			describe_mode(old_mode_desc, old_mode);
			describe_mode(new_mode_desc, new_mode);
		}

		if (chmod(argv[i], new_mode) < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			out |= 1;
			if (verbose) {
				printf("failed to change mode of '%s' from %04o (%s) to %04o (%s)\n", argv[i], old_mode, old_mode_desc, new_mode, new_mode_desc);
			}
		} else if (verbose) {
			if (new_mode == old_mode) {
				printf("mode of '%s' retained as %04o (%s)\n", argv[i], new_mode, new_mode_desc);
			} else {
				printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n", argv[i], old_mode, old_mode_desc, new_mode, new_mode_desc);
			}
		}
	}

	return out;
}
