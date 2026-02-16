/**
 * @brief Move files
 *
 * Poor implementation, mostly just 'cp' and 'rm'.
 *
 * Ideally, should figure out if it can use 'rename'... and also
 * we should implement 'rename'...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define APP_NAME "mv"
#define IS_MV

static int recursive = 1;

#include "cp.c"
#include "rm.c"

int main(int argc, char * argv[]) {
	int opt;
	int interactive = 0;
	int force = 0;

	while ((opt = getopt(argc, argv, "if")) != -1) {
		switch (opt) {
			case 'i':
				force = 0;
				interactive = 1;
				break;
			case 'f':
				force = 1;
				interactive = 0;
				break;
			default:
				fprintf(stderr, "mv: unrecognized option '%c'\n", opt);
				return 1;
		}
	}

	if (optind + 1 >= argc) {
		fprintf(stderr, "usage: %s [-if] source_file... destination\n", argv[0]);
		return 1;
	}

	char * destination = argv[argc-1];

	int target_is_dir = 0;
	struct stat statbuf;
	if (!stat(destination, &statbuf)) {
		if (S_ISDIR(statbuf.st_mode)) {
			target_is_dir = 1;
		}
	}

	int destination_has_trailing_slash = strlen(destination) && destination[strlen(destination)-1] == '/';
	int multiple_args = optind + 2 > argc;

	if (!target_is_dir && (multiple_args || destination_has_trailing_slash)) {
		fprintf(stderr, "%s: %s: Not a directory\n", argv[0], destination);
		return 1;
	}

	int ret = 0;

	for (int i = optind; i < argc - 1; ++i) {

		char * target = destination;

		if (target_is_dir) {
			char * tmp = strdup(argv[i]);
			char * target_basename = basename(tmp);
			size_t size = strlen(destination) + strlen(target_basename) + 2;
			target = malloc(size);
			snprintf(target, size, "%s%s%s", destination, destination_has_trailing_slash ? "" : "/", target_basename);
			free(tmp);
		}

		if (!force && !stat(target, &statbuf)) {
			if (interactive) { /* || (isatty(STDIN_FILENO) && some_check_for_writability...) */
				fprintf(stderr, "%s: overwrite '%s'? ", argv[0], target);
				fflush(stderr); /* just in case */
				char tmp[10] = {0};
				fgets(tmp, 10, stdin);
				if (tmp[0] != 'y' && tmp[0] != 'Y') {
					ret |= 1;
					goto _continue;
				}
			}
		}

		if (rename(argv[i], target) < 0) {
			if (errno != EXDEV && errno != ENOTSUP) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				ret |= 1;
			} else if (copy_thing(argv[i], target) || rm_thing(argv[i])) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				ret |= 1;
			}
		}

	_continue:
		if (target != destination) free(target);
	}

	return ret;
}
