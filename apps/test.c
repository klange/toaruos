/**
 * @brief test - evaluate expression
 *
 * This only supports POSIX.1-2024 options, so it doesn't have
 * any nested evaluation beyond the ! unary operator (no parens,
 * no -a, no -o, which were removed in POSIX, and no -l which
 * was never in POSIX anyway).
 *
 * The -ef option probably won't work right outside of a single
 * file system under Misaka (but any other implementation would
 * have the same problem: until we are presenting useful st_dev
 * values, lots of stuff may compare equal by dev+ino).
 *
 * You also probably don't need this - test/[ is a builtin in
 * most shells, including dash.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <sys/stat.h>

struct UnaOp {
	const char * name;
	int (*op)(char *);
};

struct BinOp {
	const char * name;
	int (*op)(char *, char *);
};

static int op_is_block(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISBLK(sb.st_mode));
}

static int op_is_char(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISCHR(sb.st_mode));
}

static int op_is_dir(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISDIR(sb.st_mode));
}

static int op_exists(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb));
}

static int op_is_file(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISREG(sb.st_mode));
}

static int op_is_setgid(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_mode & S_ISGID);
}

static int op_is_symlink(char * filename) {
	char buf[1];
	return readlink(filename, buf, 1) < 0;
}

static int op_nonzero_string(char * str) {
	return !*str;
}

static int op_is_pipe(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISFIFO(sb.st_mode));
}

static int op_is_readable(char * filename) {
	return !!access(filename, R_OK);
}

static int op_is_socket(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISSOCK(sb.st_mode));
}

static int op_nonzero_file(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_size);
}

static int op_fd_is_tty(char * fd) {
	int _fd = atoi(fd);
	return !isatty(_fd);
}

static int op_is_setuid(char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_mode & S_ISUID);
}

static int op_is_writable(char * filename) {
	return !!access(filename, W_OK);
}

static int op_is_executable(char * filename) {
	return !!access(filename, X_OK);
}

static int op_empty_string(char * str) {
	return !!*str;
}

struct UnaOp unary_ops[] = {
	{"-b", op_is_block},
	{"-c", op_is_char},
	{"-d", op_is_dir},
	{"-e", op_exists},
	{"-f", op_is_file},
	{"-g", op_is_setgid},
	{"-h", op_is_symlink},
	{"-L", op_is_symlink},
	{"-n", op_nonzero_string},
	{"-p", op_is_pipe},
	{"-r", op_is_readable},
	{"-S", op_is_socket},
	{"-s", op_nonzero_file},
	{"-t", op_fd_is_tty},
	{"-u", op_is_setuid},
	{"-w", op_is_writable},
	{"-x", op_is_executable},
	{"-z", op_empty_string},
	{NULL, NULL},
};

static int is_una(const char * op) {
	for (struct UnaOp * ops = unary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return 1;
	}
	return 0;
}

static int do_una(const char * op, char * arg) {
	for (struct UnaOp * ops = unary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return ops->op(arg);
	}
	return 0;
}

static int op_same_file(char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(left, &sbl)) return 1;
	if (stat(right, &sbr)) return 1;

	return !(sbl.st_dev == sbr.st_dev && sbl.st_ino == sbr.st_ino);
}

static int op_newer_than(char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(left, &sbl)) return 1;
	if (stat(right, &sbr)) return 0; /* if left resolves but right does not, true */

	if (sbl.st_mtime > sbr.st_mtime) return 0;
	if (sbl.st_mtime < sbr.st_mtime) return 1;
	return !(sbl.st_mtim.tv_nsec > sbr.st_mtim.tv_nsec);
}

static int op_older_than(char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(right, &sbr)) return 1;
	if (stat(left, &sbl)) return 0; /* if right but not left, true */

	if (sbl.st_mtime < sbr.st_mtime) return 0;
	if (sbl.st_mtime > sbr.st_mtime) return 1;
	return !(sbl.st_mtim.tv_nsec < sbr.st_mtim.tv_nsec);
}

static int op_str_eq(char * left, char * right) {
	return !!strcmp(left, right);
}

static int op_str_ne(char * left, char * right) {
	return !strcmp(left, right);
}

static int op_str_gt(char * left, char * right) {
	return !(strcmp(left, right) > 0);
}

static int op_str_lt(char * left, char * right) {
	return !(strcmp(left, right) < 0);
}

static long get_long(const char * val) {
	char * end;
	errno = 0;
	long l = strtol(val, &end, 10);

	if (end == val || errno) errx(2, "bad number");
	while (isspace(*end)) end++;
	if (*end) errx(2, "bad number");

	return l;
}

static int compare_ints(const char * left, const char * right) {
	long l = get_long(left);
	long r = get_long(right);
	return (l > r) ? 1 : (r > l) ? -1 : 0;
}

static int op_int_eq(char * left, char * right) { return !(compare_ints(left, right) == 0); }
static int op_int_ne(char * left, char * right) { return !(compare_ints(left, right) != 0); }
static int op_int_gt(char * left, char * right) { return !(compare_ints(left, right) > 0); }
static int op_int_ge(char * left, char * right) { return !(compare_ints(left, right) >= 0); }
static int op_int_lt(char * left, char * right) { return !(compare_ints(left, right) < 0); }
static int op_int_le(char * left, char * right) { return !(compare_ints(left, right) <= 0); }

struct BinOp binary_ops[] = {
	{"-ef", op_same_file},
	{"-nt", op_newer_than},
	{"-ot", op_older_than},

	{"=",   op_str_eq},
	{"!=",  op_str_ne},
	{">",   op_str_gt},
	{"<",   op_str_lt},

	{"-eq", op_int_eq},
	{"-ne", op_int_ne},
	{"-gt", op_int_gt},
	{"-ge", op_int_ge},
	{"-lt", op_int_lt},
	{"-le", op_int_le},

	{NULL, NULL},
};

static int is_bin(const char * op) {
	for (struct BinOp * ops = binary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return 1;
	}
	return 0;
}

static int do_bin(const char * op, char * left, char * right) {
	for (struct BinOp * ops = binary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return ops->op(left, right);
	}
	return 0;
}

static int eval(char * name, int argc, char * argv[]) {
	switch (argc) {
		case 0:
			return 1;
		case 1:
			return !**argv;
		case 2:
			if (!strcmp(argv[0],"!")) return !!*argv[1];
			if (is_una(argv[0])) return do_una(argv[0], argv[1]);
			return fprintf(stderr, "%s: %s: expected unary operator\n", name, argv[0]), 2;
		case 3:
			if (is_bin(argv[1])) return do_bin(argv[1], argv[0], argv[2]);
			if (!strcmp(argv[0],"!")) return !eval(name, argc - 1, argv + 1);
			return fprintf(stderr, "%s: %s: expected binary operator\n", name, argv[1]), 2;
		case 4:
			if (!strcmp(argv[0],"!")) return !eval(name, argc - 1, argv + 1);
			break;
	}
	return fprintf(stderr, "%s: too many arguments\n", name), 2;
}

int main(int argc, char * argv[]) {
	char * name = basename(argv[0]);
	int is_bracket = !strcmp(name, "[");

	if (is_bracket) {
		if (argc == 1 || strcmp(argv[argc-1],"]")) return fprintf(stderr, "%s: missing ']'\n", name);
		argc--;
	}

	argc--;
	argv++;

	return eval(name, argc, argv);
}
