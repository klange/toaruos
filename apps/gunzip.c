/**
 * @brief gunzip - decompress gzip-compressed payloads
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <toaru/inflate.h>

static int to_stdout = 0;
static int keep = 0;
static int force = 0;

struct GunzipCtx {
	int fd;
	char buf[32768];
	size_t written;
};

static uint8_t _get(struct inflate_context * ctx) {
	return fgetc(ctx->input_priv);
}

static void flush_it(struct GunzipCtx * mine) {
	write(mine->fd, mine->buf, mine->written);
	mine->written = 0;
}

static void _write(struct inflate_context * ctx, unsigned int sym) {
	struct GunzipCtx * mine = ctx->output_priv;
	mine->buf[mine->written++] = sym;
	if (mine->written == 32768) flush_it(mine);
}

static int usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - decompress gzip-compressed payloads\n"
			"\n"
			"usage: %s [-ckf] " X_S "name" X_E "...\n"
			"\n"
			" -c     " X_S "write to stdout; implies -k" X_E "\n"
			" -k     " X_S "keep original files unchanged" X_E "\n"
			" -f     " X_S "force decompression (eg. from tty," X_E "\n"
			"        " X_S "or to an existing file, etc.)" X_E "\n"
			"\n", argv[0], argv[0]);
	return 1;
}

static int endswith(char * str, char * suffix) {
	size_t len_suffix = strlen(suffix);
	size_t len_str = strlen(str);

	if (len_str < len_suffix) return 0;
	return !memcmp(str+len_str-len_suffix,suffix,len_suffix);
}

static int decompress_one(char * argv[], char * file) {
	FILE * f = strcmp(file, "-") ? fopen(file, "r") : stdin;

	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], file, strerror(errno));
		return 1;
	}

	if (!force && isatty(fileno(f))) {
		fprintf(stderr, "%s: not decompressng from terminal; use -f to override\n", argv[0]);
		if (f != stdin) fclose(f);
		return 1;
	}

	struct GunzipCtx mine = { 0 };
	struct inflate_context ctx;
	ctx.get_input = _get;
	ctx.write_output = _write;
	ctx.input_priv = f;
	ctx.ring = NULL;
	ctx.output_priv = &mine;


	if (f == stdin || to_stdout) {
		mine.fd = STDOUT_FILENO;
	} else {
		char * tmp = strdup(file);
		if (endswith(file,".gz")) {
			tmp[strlen(tmp)-3] = '\0';
		} else if (endswith(file,".z") || endswith(file,".Z")) {
			tmp[strlen(tmp)-2] = '\0';
		} else if (endswith(file,".tgz")) {
			tmp[strlen(tmp)-2] = 'a';
			tmp[strlen(tmp)-1] = 'r';
		} else {
			free(tmp);
			fprintf(stderr, "%s: %s: unrecognized suffix, ignoring\n", argv[0], file);
			fclose(f);
			return 1;
		}

		mine.fd = open(tmp,O_WRONLY | O_CREAT | (force ? 0 : O_EXCL));

		if (mine.fd < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], tmp, strerror(errno));
			free(tmp);
			fclose(f);
			return 1;
		}

		free(tmp);
	}

	if (gzip_decompress(&ctx)) {
		fprintf(stderr, "%s: %s: unspecified error from inflate\n", argv[0], file);
		if (mine.fd != STDOUT_FILENO) close(mine.fd);
		if (f != stdin) fclose(f);
		return 1;
	}

	flush_it(&mine);
	if (f != stdin) fclose(f);

	if (mine.fd != STDOUT_FILENO) close(mine.fd);
	if (f != stdin && !keep) unlink(file); /* Just ignore errors, whatever. */

	return 0;
}

int main(int argc, char * argv[]) {

	int opt;
	while ((opt = getopt(argc, argv, "?ckf")) != -1) {
		switch (opt) {
			case 'c':
				to_stdout = 1;
				keep = 1;
				break;
			case 'k':
				keep = 1;
				break;
			case 'f':
				force = 1;
				break;
			default:
			case '?':
				return usage(argc, argv);
		}
	}

	/* No argument, read from stdin */
	if (optind >= argc) {
		return decompress_one(argv, "-");
	} else {
		int ret = 0;
		for (int i = optind; i < argc; ++i) {
			ret |= decompress_one(argv, argv[i]);
		}
		return ret;
	}
}

