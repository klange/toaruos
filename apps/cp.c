/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * cp - Copy files
 *
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define CHUNK_SIZE 4096

static int recursive = 0;
static int symlinks = 0;
static int copy_thing(char * tmp, char * tmp2);

static int copy_link(char * source, char * dest, int mode, int uid, int gid) {
	//fprintf(stderr, "need to copy link %s to %s\n", source, dest);
	char tmp[1024];
	readlink(source, tmp, 1024);
	symlink(tmp, dest);
	chmod(dest, mode);
	chown(dest, uid, gid);

	return 0;
}

static int copy_file(char * source, char * dest, int mode,int uid, int gid) {
	//fprintf(stderr, "need to copy file %s to %s %x\n", source, dest, mode);

	int d_fd = open(dest, O_WRONLY | O_CREAT, mode);
	int s_fd = open(source, O_RDONLY);

	ssize_t length;

	length = lseek(s_fd, 0, SEEK_END);
	lseek(s_fd, 0, SEEK_SET);

	//fprintf(stderr, "%d bytes to copy\n", length);

	char buf[CHUNK_SIZE];

	while (length > 0) {
		size_t r = read(s_fd, buf, length < CHUNK_SIZE ? length : CHUNK_SIZE);
		//fprintf(stderr, "copying %d bytes from %s to %s\n", r, source, dest);
		write(d_fd, buf, r);
		length -= r;
		//fprintf(stderr, "%d bytes remaining\n", length);
	}

	close(s_fd);
	close(d_fd);

	chown(dest, uid, gid);
	return 0;
}

static int copy_directory(char * source, char * dest, int mode, int uid, int gid) {
	DIR * dirp = opendir(source);
	if (dirp == NULL) {
		fprintf(stderr, "Failed to copy directory %s\n", source);
		return 1;
	}

	//fprintf(stderr, "Creating %s\n", dest);
	if (!strcmp(dest, "/")) {
		dest = "";
	} else {
		mkdir(dest, mode);
	}

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) {
			//fprintf(stderr, "Skipping %s\n", ent->d_name);
			ent = readdir(dirp);
			continue;
		}
		//fprintf(stderr, "not skipping %s/%s → %s/%s\n", source, ent->d_name, dest, ent->d_name);
		char tmp[strlen(source)+strlen(ent->d_name)+2];
		sprintf(tmp, "%s/%s", source, ent->d_name);
		char tmp2[strlen(dest)+strlen(ent->d_name)+2];
		sprintf(tmp2, "%s/%s", dest, ent->d_name);
		//fprintf(stderr,"%s → %s\n", tmp, tmp2);
		copy_thing(tmp,tmp2);
		ent = readdir(dirp);
	}
	closedir(dirp);

	chown(dest, uid, gid);

	return 0;
}

static int copy_thing(char * tmp, char * tmp2) {
	struct stat statbuf;
	if (symlinks) {
		lstat(tmp,&statbuf);
	} else {
		stat(tmp,&statbuf);
	}
	if (S_ISLNK(statbuf.st_mode)) {
		return copy_link(tmp, tmp2, statbuf.st_mode & 07777, statbuf.st_uid, statbuf.st_gid);
	} else if (S_ISDIR(statbuf.st_mode)) {
		if (!recursive) {
			fprintf(stderr, "cp: %s: omitting directory\n", tmp);
			return 1;
		}
		return copy_directory(tmp, tmp2, statbuf.st_mode & 07777, statbuf.st_uid, statbuf.st_gid);
	} else if (S_ISREG(statbuf.st_mode)) {
		return copy_file(tmp, tmp2, statbuf.st_mode & 07777, statbuf.st_uid, statbuf.st_gid);
	} else {
		fprintf(stderr, "cp: %s is not any of the required file types?\n", tmp);
		return 1;
	}
}

int main(int argc, char ** argv) {

	int opt;
	while ((opt = getopt(argc, argv, "RrP")) != -1) {
		switch (opt) {
			case 'R':
			case 'r':
				recursive = 1;
				symlinks = 1;
				break;
			case 'P':
				symlinks = 0;
				break;
			default:
				fprintf(stderr, "cp: unrecognized option '%c'\n", opt);
				break;
		}
	}

	if (optind < argc - 1) {
		char * destination = argv[argc-1];

		struct stat statbuf;
		stat((destination), &statbuf);
		if (S_ISDIR(statbuf.st_mode)) {
			while (optind < argc - 1) {
				char * source = strrchr(argv[optind], '/');
				if (!source) source = argv[optind];
				char output[4096];
				sprintf(output, "%s/%s", destination, source);
				copy_thing(argv[optind], output);
				optind++;
			}
		} else {
			if (optind < argc - 2) {
				fprintf(stderr, "cp: target '%s' is not a directory\n", destination);
				return 1;
			}
			copy_thing(argv[optind], destination);
		}
	} else {
		fprintf(stderr, "cp: not enough arguments\n");
	}

	return 0;
}

