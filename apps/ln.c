/**
 * @brief ln - create links
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <libgen.h>
#include <sys/stat.h>

static int show_usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-fsn] [-L|-P] link_name target_file\n"
		"       %s [-fsn] [-L|-P] link_name... target_dir\n",
		argv[0], argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {

	static struct option long_opts[] = {
		{"physical",no_argument,0,'P'},
		{"logical",no_argument,0,'L'},
		{"no-dereference",no_argument,0,'n'},
		{"force",no_argument,0,'f'},
		{"symbolic",no_argument,0,'s'},
		{"help",no_argument,0,1000},
		{0,0,0,0},
	};

	int follow_symlink = 0; /* default to -P */
	int no_dereference = 0;
	int force = 0;
	int symbolic = 0;

	int opt;

	while ((opt = getopt_long(argc, argv, "nfPLs", long_opts, NULL)) != -1) {
		switch (opt) {
			case 'P':
				follow_symlink = 0;
				break;
			case 'L':
				follow_symlink = 1;
				break;
			case 'n':
				no_dereference = 1;
				break;
			case 'f':
				force = 1;
				break;
			case 's':
				symbolic = 1;
				break;
			case 1000:
				return show_usage(argv), 0;
			default:
				return show_usage(argv);
		}
	}

	if (optind + 2 > argc) return show_usage(argv);
	int target_is_dir = (optind + 2 < argc);

	char * target_file = argv[argc-1];

	int fd = openat(AT_FDCWD, target_file, O_RDONLY | O_DIRECTORY | (no_dereference ? O_NOFOLLOW : 0));

	if (fd < 0 && target_is_dir) err(1, "%s", target_file);
	if (fd >= 0) target_is_dir = 1;

	if (!target_is_dir) fd = AT_FDCWD;

	int out = 0;

	for (int i = optind; i < argc - 1; ++i) {
		char * tname = target_file;
		if (target_is_dir) tname = basename(argv[i]);
		int ret = 0;
		if (force && !faccessat(fd, tname, F_OK, AT_SYMLINK_NOFOLLOW)) {
			struct stat a, b; /* Make sure it's not the file we're about to link... */
			if (!fstatat(fd, tname, &a, AT_SYMLINK_NOFOLLOW) &&
			    !fstatat(AT_FDCWD, argv[i], &b, AT_SYMLINK_NOFOLLOW) &&
			    a.st_dev == b.st_dev &&
			    a.st_ino == b.st_ino) {
				out |= 1;
				warnx("'%s' and '%s' are the same file", argv[i], tname);
				continue;
			}
			ret = unlinkat(fd, tname, 0);
		}
		if (!ret) {
			if (symbolic) {
				ret = symlinkat(argv[i], fd, tname);
			} else {
				ret = linkat(AT_FDCWD, argv[i], fd, tname, follow_symlink ? AT_SYMLINK_FOLLOW : 0);
			}
		}

		if (ret < 0) {
			out |= 1;
			warn("%s", argv[i]);
		}
	}

	return out;
}
