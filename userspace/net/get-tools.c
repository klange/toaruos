/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 Kevin Lange
 *
 * get-tools - Retreive packages from ToaruOS site and install them
 *             appropriately into a tmpfs.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lib/sha2.h"
#include "lib/hashmap.h"

#define TOARUOS_ORG_URL "http://toaruos.org/"

static hashmap_t * hashes;

static void fetch_file(const char * url, const char * output_path, int check_sig) {
	fprintf(stderr, "Fetching %s...", url);
	fflush(stderr);
	char tmp[1024];
	sprintf(tmp, "fetch -o \"%s\" \"" TOARUOS_ORG_URL "%s\"", output_path, url);
	system(tmp);
	if (check_sig) {
		fprintf(stderr, " Checking signature...");
		fflush(stderr);

		FILE * f = fopen(output_path, "r");
		char hash[SHA512_DIGEST_STRING_LENGTH];

		SHA512_CTX ctx;
		SHA512_Init(&ctx);

		while (!feof(f)) {
			char buf[1024];
			size_t r = fread(buf, 1, 1024, f);
			SHA512_Update(&ctx, buf, r);
		}

		SHA512_End(&ctx, hash);

		if (strcmp(hash, hashmap_get(hashes, (char *)url))) {
			fprintf(stderr, " ✗ (sha mismatch)\n");
		}
	}
	fprintf(stderr, " ✔\n");
}

static void mark_executable(const char * file) {
	struct stat s;
	stat(file, &s);
	chmod(file, s.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
}

static void read_signatures(void) {
	hashes = hashmap_create(10);

	FILE * shas = fopen("/tmp/shasums","r");

	while (!feof(shas)) {
		char sha[129], file[50];
		fscanf(shas, "\n%s  %s", &sha, &file);
		hashmap_set(hashes, file, strdup(sha));
	}

	fclose(shas);
}

static void make_tmpfs(const char * path) {
	char tmp[1024];
	sprintf(tmp, "mount tmpfs x \"%s\"", path);
	system(tmp);
}

static void mount_ext2(const char * img, const char * path) {
	char tmp[1024];
	sprintf(tmp, "mount ext2 \"%s\" \"%s\"", img, path);
	system(tmp);
}

int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		fprintf(stderr, "%s: Please run as root.\n", argv[0]);
		return 1;
	}

	fetch_file("shasums","/tmp/shasums",0);
	read_signatures();
	make_tmpfs("/usr/bin");

	/* Vim */
	fetch_file("apps/vim","/usr/bin/vim",1);
	mark_executable("/usr/bin/vim");
	fetch_file("apps/vimfiles.img","/tmp/vimfiles.img",1);
	mount_ext2("/tmp/vimfiles.img", "/usr/share/vim");

	/* Bochs */
	fetch_file("apps/bochs","/usr/bin/bochs",1);
	mark_executable("/usr/bin/bochs");
	make_tmpfs("/usr/share/bochs");
	fetch_file("bochs/bios","/usr/share/bochs/BIOS-bochs-latest",1);
	fetch_file("bochs/vgabios","/usr/share/bochs/VGABIOS-lgpl-latest",1);

	/* YASM */
	fetch_file("bochs/yasm","/usr/bin/yasm",1);
	mark_executable("/usr/bin/yasm");

	/* Lua */
	fetch_file("apps/lua","/usr/bin/lua",1);
	mark_executable("/usr/bin/lua");

	return 0;
}
