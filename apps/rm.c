/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * rm - Unlink files
 *
 * TODO: Support recursive, directory removal, etc.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

static int recursive = 0;
static int rm_thing(char * tmp);

static int rm_directory(char * source) {
	DIR * dirp = opendir(source);
	if (dirp == NULL) {
		fprintf(stderr, "could not open %s\n", source);
		return 1;
	}

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) {
			ent = readdir(dirp);
			continue;
		}
		char tmp[strlen(source)+strlen(ent->d_name)+2];
		sprintf(tmp, "%s/%s", source, ent->d_name);
		int status = rm_thing(tmp);
		if (status) return status;
		ent = readdir(dirp);
	}
	closedir(dirp);

	return unlink(source);
}

static int rm_thing(char * tmp) {
	struct stat statbuf;
	lstat(tmp,&statbuf);
	if (S_ISDIR(statbuf.st_mode)) {
		if (!recursive) {
			fprintf(stderr, "rm: %s: is a directory\n", tmp);
			return 1;
		}
		return rm_directory(tmp);
	} else {
		return unlink(tmp);
	}
}


int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "r")) != -1) {
		switch (opt) {
			case 'r':
				recursive = 1;
				break;
			default:
				fprintf(stderr, "rm: unrecognized option '%c'\n", opt);
				break;
		}
	}

	int ret = 0;

	for (int i = optind; i < argc; ++i) {
		ret |= rm_thing(argv[i]);
	}

	return ret;
}
