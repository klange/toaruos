/**
 * @brief Display file status.
 *
 * The format for this is terrible and we're missing a bunch
 * of data we provide in our statbuf...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/time.h>

static int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - display file status\n"
			"\n"
			"usage: %s [-Lq] " X_S "PATH" X_E "...\n"
			"\n"
			" -L     " X_S "dereference symlinks" X_E "\n"
			" -q     " X_S "don't print anything, just return 0 if file exists" X_E "\n"
			" -?     " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0]);
	return 1;
}

static int dereference = 0, quiet = 0;

static int stat_file(char * file) {

	struct stat _stat;
	int result;

	if (dereference) {
		result = stat(file, &_stat);
	} else {
		result = lstat(file, &_stat);
	}

	if (result == -1) {
		if (!quiet) {
			fprintf(stderr, "stat: %s: %s\n", file, strerror(errno));
		}
		return 1;
	}

	if (quiet) return 0;

	const char * file_type = "regular file";
	if (S_ISDIR(_stat.st_mode))        file_type = "directory";
	else if (S_ISFIFO(_stat.st_mode))  file_type = "fifo";
	else if (S_ISLNK(_stat.st_mode))   file_type = "symbolic link";
	else if (S_ISBLK(_stat.st_mode))   file_type = "block device";
	else if (S_ISCHR(_stat.st_mode))   file_type = "character device";

	struct stat * f = &_stat;

	printf("  File: %s\n", file);
	/* TODO: st_blocks is not being set, skip it */
	printf("  Size: %-10lu %s\n", f->st_size, file_type);
	printf("Device: %-10lu Inode: %-10lu  Links: %u\n", f->st_dev, f->st_ino, f->st_nlink);
	printf("Access: ");
	/* Copied from apps/ls.c */
	if (S_ISLNK(f->st_mode))       { printf("l"); }
	else if (S_ISCHR(f->st_mode))  { printf("c"); }
	else if (S_ISBLK(f->st_mode))  { printf("b"); }
	else if (S_ISDIR(f->st_mode))  { printf("d"); }
	else { printf("-"); }
	printf( (f->st_mode & S_IRUSR) ? "r" : "-");
	printf( (f->st_mode & S_IWUSR) ? "w" : "-");
	printf( (f->st_mode & S_ISUID) ? "s" : ((f->st_mode & S_IXUSR) ? "x" : "-"));
	printf( (f->st_mode & S_IRGRP) ? "r" : "-");
	printf( (f->st_mode & S_IWGRP) ? "w" : "-");
	printf( (f->st_mode & S_IXGRP) ? "x" : "-");
	printf( (f->st_mode & S_IROTH) ? "r" : "-");
	printf( (f->st_mode & S_IWOTH) ? "w" : "-");
	printf( (f->st_mode & S_IXOTH) ? "x" : "-");
	printf(" Uid: %-8u Gid: %-8u\n", f->st_uid, f->st_gid);

	char time_buf[80];
	strftime(time_buf, 80, "%c", localtime((time_t*)&f->st_atime));
	printf("Access: %s\n", time_buf);
	strftime(time_buf, 80, "%c", localtime((time_t*)&f->st_mtime));
	printf("Modify: %s\n", time_buf);
	strftime(time_buf, 80, "%c", localtime((time_t*)&f->st_ctime));
	printf("Change: %s\n", time_buf);

	return 0;

}

int main(int argc, char ** argv) {
	int opt;

	while ((opt = getopt(argc, argv, "?Lq")) != -1) {
		switch (opt) {
			case 'L':
				dereference = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case '?':
				return show_usage(argc,argv);
		}
	}

	if (optind >= argc) {
		return show_usage(argc, argv);
	}

	int ret = 0;

	while (optind < argc) {
		ret |= stat_file(argv[optind]);
		optind++;
	}

	return ret;

}

