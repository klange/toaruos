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

#include <toaru/inflate.h>

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
	char padding[12];
};

static struct ustar * extract_file(FILE * f) {
	static struct ustar _ustar;
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
static void write_file(struct ustar * file, FILE * f, FILE * mf, char * name) {
	size_t length = interpret_size(file);
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
	if (mf != stdout) {
		fclose(mf);
		chmod(name, interpret_mode(file));
	}
}

static void _seek_forward(FILE * f, size_t amount) {
	for (size_t i = 0; i < amount; ++i) {
		fgetc(f);
	}
}

static void usage(char * argv[]) {
	fprintf(stderr,
			"tar - extract ustar archives\n"
			"\n"
			"usage: %s [-ctxvaf] [name]\n"
			"\n"
			" -f     \033[3mfile archive to open\033[0m\n"
			" -x     \033[3mextract\033[0m\n"
			"\n", argv[0]);
}

static int matches_files(int argc, char * argv[], int optind, char * filename) {
	while (optind < argc) {
		if (!strcmp(argv[optind], filename)) return 1;
		optind++;
	}

	return 0;
}

int main(int argc, char * argv[]) {

	int opt;
	char * fname = NULL;
	int verbose = 0;
	int action = 0;
	int compressed = 0;
	int to_stdout = 0;
	int only_matches = 0;
#define TAR_ACTION_EXTRACT 1
#define TAR_ACTION_CREATE  2
#define TAR_ACTION_LIST    3

	while ((opt = getopt(argc, argv, "?ctxzvaf:O")) != -1) {
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
			case 'z':
				compressed = 1;
				break;
			case 'O':
				to_stdout = 1;
				break;
			case '?':
				usage(argv);
				return 1;
			default:
				fprintf(stderr, "%s: unsupported option '%c'\n", argv[0], opt);
				return 1;
		}
	}

	if (!fname) {
		fname = "-";
	}

	if (optind < argc) {
		only_matches = 1;
	}

	if (action == TAR_ACTION_EXTRACT || action == TAR_ACTION_LIST) {

		FILE * f;
		if (!strcmp(fname,"-")) {
			f = stdin;
		} else {
			f = fopen(fname,"r");
		}
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], fname, strerror(errno));
			return 1;
		}

		if (compressed) {
			int fds[2];
			pipe(fds);

			int child = fork();
			if (child == 0) {
				/* Close the read end */
				close(fds[0]);
				/* Put f's fd into stdin */
				dup2(fileno(f), STDIN_FILENO);
				/* Make stdout the pipe */
				dup2(fds[1], STDOUT_FILENO);
				/* Execeute gzunzip */
				char * args[] = {"gunzip","-c",NULL};
				exit(execvp("gunzip",args));
			} else if (child < 0) {
				fprintf(stderr, "%s: failed to fork gunzip for compressed archive\n", argv[0]);
				return 1;
			}

			/* Reattach f to pipe */
			close(fds[1]);
			f = fdopen(fds[0], "r");
		}

		char tmpname[1024] = {0};
		int  last_was_long = 0;

		while (!feof(f)) {
			struct ustar * file = extract_file(f);

			if (!file) {
				break;
			}

			if (action == TAR_ACTION_LIST) {
				if (verbose) {
					fprintf(stdout, "%10d %c %.155s%.100s\n", interpret_size(file), file->type[0], file->prefix, file->filename);
				} else {
					fprintf(stdout, "%.155s%.100s\n", file->prefix, file->filename);
				}
				_seek_forward(f, interpret_size(file));
			} else if (action == TAR_ACTION_EXTRACT) {
				if (verbose) {
					fprintf(stdout, "%.155s%.100s\n", file->prefix, file->filename);
				}
				char name[1024] = {0};
				if (last_was_long) {
					strncat(name, tmpname, 1023);
					last_was_long = 0;
				} else {
					strncat(name, file->prefix, 155);
					strncat(name, file->filename, 100);
				}

				if (file->type[0] == '0' || file->type[0] == 0) {
					FILE * mf = to_stdout ? stdout : fopen(name,"w");
					if (!mf) {
						fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
						_seek_forward(f, interpret_size(file));
					} else {
						if (!only_matches || matches_files(argc,argv,optind,name)) {
							write_file(file,f,mf,name);
						}
					}
				} else if (file->type[0] == '5') {
					if (!to_stdout) {
						if (name[strlen(name)-1] == '/') {
							name[strlen(name)-1] = '\0';
						}
						if (strlen(name)) {
							if (!only_matches || matches_files(argc,argv,optind,name)) {
								if (mkdir(name, 0777) < 0) {
									if (errno != EEXIST) {
										fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
									}
								}
							}
						}
					}
				} else if (file->type[0] == '1') {
					if (!to_stdout && (!only_matches || matches_files(argc,argv,optind,name))) {
						char tmp[101] = {0};
						strncat(tmp, file->link, 100);
						FILE * mf = fopen(name,"w");
						if (!mf) {
							fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, strerror(errno));
						} else {
							FILE * source = fopen(tmp, "r");
							if (!source) {
								fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, tmp, strerror(errno));
							} else {
								while (!feof(source)) {
									char buf[4096];
									ssize_t r = fread(buf, 1, 4096, source);
									fwrite(buf, 1, r, mf);
								}
								fclose(source);
							}
							fclose(mf);
							chmod(name, interpret_mode(file));
						}
					}
					_seek_forward(f, interpret_size(file));
				} else if (file->type[0] == '2') {
					if (!to_stdout && (!only_matches || matches_files(argc,argv,optind,name))) {
						char tmp[101] = {0};
						strncat(tmp, file->link, 100);
						if (symlink(tmp, name) < 0) {
							fprintf(stderr, "%s: %s: %s: %s: %s\n", argv[0], fname, name, tmp, strerror(errno));
						}
					}
					_seek_forward(f, interpret_size(file));
				} else if (file->type[0] == 'L') {
					/* This is a GNU Long Name block; store its contents as a file name */
					size_t s = interpret_size(file);
					fread(tmpname, 1, s, f);
					tmpname[s] = '\0';
					last_was_long = 1;
				} else {
					fprintf(stderr, "%s: %s: %s: %s\n", argv[0], fname, name, type_to_string(file->type[0]));
					_seek_forward(f, interpret_size(file));
				}
			}

			size_t file_size = interpret_size(file);
			if (file_size % 512) {
				_seek_forward(f, 512 - (file_size % 512));
			}
		}
	} else {
		fprintf(stderr, "%s: unsupported action\n", argv[0]);
		return 1;
	}

	return 0;
}
