/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * tar - extract archives
 *
 * This is a very minimal and incomplete implementation of tar.
 * It supports on ustar-formatted archives, and its arguments
 * must by the - forms. As of writing, creating archives is not
 * supported. No compression formats are supported, either.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <toaru/hashmap.h>

struct ustar {
	char filename[100];
	char mode[8];
	char ownerid[8];
	char groupid[8];

	char size[12];
	char mtime[12];

	char checksum[8];
	char type[1];
	char link[100];

	char ustar[6];
	char version[2];

	char owner[32];
	char group[32];

	char dev_major[8];
	char dev_minor[8];

	char prefix[155];
};

static struct ustar * file_from_offset(FILE * f, size_t offset) {
	static struct ustar _ustar;
	fseek(f, offset, SEEK_SET);
	if (fread(&_ustar, 1, sizeof(struct ustar), f) != sizeof(struct ustar)) {
		fprintf(stderr, "failed to read file\n");
		return NULL;
	}

	if (_ustar.ustar[0] != 'u' ||
		_ustar.ustar[1] != 's' ||
		_ustar.ustar[2] != 't' ||
		_ustar.ustar[3] != 'a' ||
		_ustar.ustar[4] != 'r') {
		return NULL;
	}
	return &_ustar;
}

static unsigned int round_to_512(unsigned int i) {
	unsigned int t = i % 512;

	if (!t) return i;
	return i + (512 - t);
}

static unsigned int interpret_mode(struct ustar * file) {
	return 
		((file->mode[0] - '0') << 18) |
		((file->mode[1] - '0') << 15) |
		((file->mode[2] - '0') << 12) |
		((file->mode[3] - '0') <<  9) |
		((file->mode[4] - '0') <<  6) |
		((file->mode[5] - '0') <<  3) |
		((file->mode[6] - '0') <<  0);
}

static unsigned int interpret_size(struct ustar * file) {
	if (file->size[0] != '0') {
		fprintf(stderr, "\033[3;32mWarning:\033[0;3m File is too big.\033[0m\n");
	}
	return
		((file->size[ 0] - '0') << 30) |
		((file->size[ 1] - '0') << 27) |
		((file->size[ 2] - '0') << 24) |
		((file->size[ 3] - '0') << 21) |
		((file->size[ 4] - '0') << 18) |
		((file->size[ 5] - '0') << 15) |
		((file->size[ 6] - '0') << 12) |
		((file->size[ 7] - '0') <<  9) |
		((file->size[ 8] - '0') <<  6) |
		((file->size[ 9] - '0') <<  3) |
		((file->size[10] - '0') <<  0);
}

static const char * type_to_string(char type) {
	static char unknown[100];
	switch (type) {
		case '\0':
		case '0':
			return "Normal file";
		case '1':
			return "Hard link (unsupported)";
		case '2':
			return "Symolic link";
		case '3':
			return "Character special (unsupported)";
		case '4':
			return "Block special (unsupported)";
		case '5':
			return "Directory";
		case '6':
			return "FIFO (unsupported)";
		case 'g':
			return "Extended header";
		case 'x':
			return "Extended preheader";
		default:
			sprintf(unknown, "Uknown: %c", type);
			return unknown;
	}
}

#if 0
static void dump_file(struct ustar * file) {
	fprintf(stdout, "\033[1m%.155s%.100s\033[0m\n", file->prefix, file->filename);
	fprintf(stdout, "%c - %s\n", file->type[0], type_to_string(file->type[0]));
	fprintf(stdout, "File size: %u\n", interpret_size(file));
}
#endif

#define CHUNK_SIZE 4096
static void write_file(struct ustar * file, FILE * f, FILE * mf, size_t off, char * name) {
	size_t length = interpret_size(file);
	fseek(f, off + 512, SEEK_SET);
	char buf[CHUNK_SIZE];
	while (length > CHUNK_SIZE) {
		fread( buf, 1, CHUNK_SIZE, f);
		fwrite(buf, 1, CHUNK_SIZE, mf);
		length -= CHUNK_SIZE;
	}
	if (length > 0) {
		fread( buf, 1, length, f);
		fwrite(buf, 1, length, mf);
	}
	fclose(mf);
	fseek(f, off, SEEK_SET);
	/* TODO: fchmod? */
	chmod(name, interpret_mode(file));
}

int main(int argc, char * argv[]) {

	int opt;
	char * fname = NULL;
	int verbose = 0;
	int action = 0;
#define TAR_ACTION_EXTRACT 1
#define TAR_ACTION_CREATE  2
#define TAR_ACTION_LIST    3

	while ((opt = getopt(argc, argv, "ctxvaf:")) != -1) {
		switch (opt) {
			case 'c':
				if (action) {
					fprintf(stderr, "%s: %c: already specified action\n", argv[0], opt);
					return 1;
				}
				action = TAR_ACTION_CREATE;
				break;
			case 'f':
				fname = optarg;
				break;
			case 'x':
				if (action) {
					fprintf(stderr, "%s: %c: already specified action\n", argv[0], opt);
					return 1;
				}
				action = TAR_ACTION_EXTRACT;
				break;
			case 't':
				if (action) {
					fprintf(stderr, "%s: %c: already specified action\n", argv[0], opt);
					return 1;
				}
				action = TAR_ACTION_LIST;
				break;
			case 'v':
				verbose = 1;
				break;
			default:
				fprintf(stderr, "%s: unsupported option '%c'\n", argv[0], opt);
				return 1;
		}
	}

	if (!fname) {
		fprintf(stderr, "%s: todo: stdin/stdout\n", argv[0]);
		return 1;
	}

	if (action == TAR_ACTION_EXTRACT || action == TAR_ACTION_LIST) {

		hashmap_t * files = hashmap_create(10);

		FILE * f = fopen(fname,"r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], fname, strerror(errno));
			return 1;
		}

		fseek(f, 0, SEEK_END);
		size_t length = ftell(f);
		fseek(f, 0, SEEK_SET);

		char tmpname[1024] = {0};
		int  last_was_long = 0;

		size_t off = 0;
		while (!feof(f)) {
			struct ustar * file = file_from_offset(f, off);

			if (!file) {
				break;
			}

			if (action == TAR_ACTION_LIST || verbose) {
				fprintf(stdout, "%.155s%.100s\n", file->prefix, file->filename);
			}

			if (action == TAR_ACTION_EXTRACT) {
				char name[1024] = {0};
				if (last_was_long) {
					strncat(name, tmpname, 1023);
					last_was_long = 0;
				} else {
					strncat(name, file->prefix, 155);
					strncat(name, file->filename, 100);
				}

				if (file->type[0] == '0' || file->type[0] == 0) {
					FILE * mf = fopen(name,"w");
					if (!mf) {
						fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
					} else {
						write_file(file,f,mf,off,name);
					}
					struct ustar * tmp = malloc(sizeof(struct ustar));
					memcpy(tmp, file, sizeof(struct ustar));
					hashmap_set(files, name, tmp);
				} else if (file->type[0] == '5') {
					if (name[strlen(name)-1] == '/') {
						name[strlen(name)-1] = '\0';
					}
					if (strlen(name)) {
						if (mkdir(name, 0777) < 0) {
							if (errno != EEXIST) {
								fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
							}
						}
					}
				} else if (file->type[0] == '1') {
					char tmp[101] = {0};
					strncat(tmp, file->link, 100);
					if (!hashmap_has(files, tmp)) {
						fprintf(stderr, "%s: %s: %s: %s: missing target\n", argv[0], fname, name, tmp);
					} else {
						FILE * mf = fopen(name,"w");
						if (!mf) {
							fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
						} else {
							write_file(hashmap_get(files,tmp),f,mf,off,name);
						}
					}
				} else if (file->type[0] == '2') {
					char tmp[101] = {0};
					strncat(tmp, file->link, 100);
					if (symlink(tmp, name) < 0) {
						fprintf(stderr, "%s: %s: %s: %s: %s\n", argv[0], fname, name, tmp, strerror(errno));
					}
				} else if (file->type[0] == 'L') {
					/* This is a GNU Long Name block; store its contents as a file name */
					size_t s = interpret_size(file);
					fseek(f, off + 512, SEEK_SET);
					fread(tmpname, 1, s, f);
					tmpname[s] = '\0';
					fseek(f, off, SEEK_SET);
					last_was_long = 1;
				} else {
					fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, type_to_string(file->type[0]));
				}
			}

			off += 512;
			off += round_to_512(interpret_size(file));
			if (off >= length) break;
		}
	} else {
		fprintf(stderr, "%s: unsupported action\n", argv[0]);
		return 1;
	}

	return 0;
}
