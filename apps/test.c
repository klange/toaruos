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
#include <sys/stat.h>

struct TestUnaOp {
	const char * name;
	int (*op)(const char *, char *);
};

struct TestBinOp {
	const char * name;
	int (*op)(const char *, char *, char *);
};

static int test_op_is_block(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISBLK(sb.st_mode));
}

static int test_op_is_char(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISCHR(sb.st_mode));
}

static int test_op_is_dir(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISDIR(sb.st_mode));
}

static int test_op_exists(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb));
}

static int test_op_is_file(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISREG(sb.st_mode));
}

static int test_op_is_setgid(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_mode & S_ISGID);
}

static int test_op_is_symlink(const char * name, char * filename) {
	char buf[1];
	return readlink(filename, buf, 1) < 0;
}

static int test_op_nonzero_string(const char * name, char * str) {
	return !*str;
}

static int test_op_is_pipe(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISFIFO(sb.st_mode));
}

static int test_op_is_readable(const char * name, char * filename) {
	return !!access(filename, R_OK);
}

static int test_op_is_socket(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && S_ISSOCK(sb.st_mode));
}

static int test_op_nonzero_file(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_size);
}

static int test_op_fd_is_tty(const char * name, char * fd) {
	int _fd = atoi(fd);
	return !isatty(_fd);
}

static int test_op_is_setuid(const char * name, char * filename) {
	struct stat sb;
	return !(!stat(filename, &sb) && sb.st_mode & S_ISUID);
}

static int test_op_is_writable(const char * name, char * filename) {
	return !!access(filename, W_OK);
}

static int test_op_is_executable(const char * name, char * filename) {
	return !!access(filename, X_OK);
}

static int test_op_empty_string(const char * name, char * str) {
	return !!*str;
}

struct TestUnaOp test_unary_ops[] = {
	{"-b", test_op_is_block},
	{"-c", test_op_is_char},
	{"-d", test_op_is_dir},
	{"-e", test_op_exists},
	{"-f", test_op_is_file},
	{"-g", test_op_is_setgid},
	{"-h", test_op_is_symlink},
	{"-L", test_op_is_symlink},
	{"-n", test_op_nonzero_string},
	{"-p", test_op_is_pipe},
	{"-r", test_op_is_readable},
	{"-S", test_op_is_socket},
	{"-s", test_op_nonzero_file},
	{"-t", test_op_fd_is_tty},
	{"-u", test_op_is_setuid},
	{"-w", test_op_is_writable},
	{"-x", test_op_is_executable},
	{"-z", test_op_empty_string},
	{NULL, NULL},
};

static int test_is_una(const char * op) {
	for (struct TestUnaOp * ops = test_unary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return 1;
	}
	return 0;
}

static int test_do_una(const char * name, const char * op, char * arg) {
	for (struct TestUnaOp * ops = test_unary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return ops->op(name, arg);
	}
	return 0;
}

static int test_op_same_file(const char * name, char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(left, &sbl)) return 1;
	if (stat(right, &sbr)) return 1;

	return !(sbl.st_dev == sbr.st_dev && sbl.st_ino == sbr.st_ino);
}

static int test_op_newer_than(const char * name, char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(left, &sbl)) return 1;
	if (stat(right, &sbr)) return 0; /* if left resolves but right does not, true */

	if (sbl.st_mtime > sbr.st_mtime) return 0;
	if (sbl.st_mtime < sbr.st_mtime) return 1;
	return !(sbl.st_mtim.tv_nsec > sbr.st_mtim.tv_nsec);
}

static int test_op_older_than(const char * name, char * left, char * right) {
	struct stat sbl, sbr;

	if (stat(right, &sbr)) return 1;
	if (stat(left, &sbl)) return 0; /* if right but not left, true */

	if (sbl.st_mtime < sbr.st_mtime) return 0;
	if (sbl.st_mtime > sbr.st_mtime) return 1;
	return !(sbl.st_mtim.tv_nsec < sbr.st_mtim.tv_nsec);
}

static int test_op_str_eq(const char * name, char * left, char * right) {
	return !!strcmp(left, right);
}

static int test_op_str_ne(const char * name, char * left, char * right) {
	return !strcmp(left, right);
}

static int test_op_str_gt(const char * name, char * left, char * right) {
	return !(strcmp(left, right) > 0);
}

static int test_op_str_lt(const char * name, char * left, char * right) {
	return !(strcmp(left, right) < 0);
}

static int test_get_long(const char * val, long * out) {
	char * end;
	errno = 0;
	long l = strtol(val, &end, 10);

	if (end == val || errno) return 2;
	while (isspace(*end)) end++;
	if (*end) return 2;

	*out = l;

	return 0;
}

static int test_compare_ints(const char * name, const char * left, const char * right) {
	long l, r;
	if (test_get_long(left, &l)) return fprintf(stderr, "%s: bad number: %s\n", name, left), 2;
	if (test_get_long(right, &r)) return fprintf(stderr, "%s: bad number: %s\n", name, right), 2;
	return (l > r) ? 1 : (r > l) ? -1 : 0;
}

#define make_compar(name, op) \
	static int test_op_int_ ## name (const char * name, char * left, char * right) { \
		int res = test_compare_ints(name, left, right); \
		if (res == 2) return 2; \
		return !(res op 0); \
	}

make_compar(eq,==)
make_compar(ne,!=)
make_compar(gt,>)
make_compar(ge,>=)
make_compar(lt,<)
make_compar(le,<=)
#undef make_compar

struct TestBinOp test_binary_ops[] = {
	{"-ef", test_op_same_file},
	{"-nt", test_op_newer_than},
	{"-ot", test_op_older_than},

	{"=",   test_op_str_eq},
	{"!=",  test_op_str_ne},
	{">",   test_op_str_gt},
	{"<",   test_op_str_lt},

	{"-eq", test_op_int_eq},
	{"-ne", test_op_int_ne},
	{"-gt", test_op_int_gt},
	{"-ge", test_op_int_ge},
	{"-lt", test_op_int_lt},
	{"-le", test_op_int_le},

	{NULL, NULL},
};

static int test_is_bin(const char * op) {
	for (struct TestBinOp * ops = test_binary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return 1;
	}
	return 0;
}

static int test_do_bin(const char * name, const char * op, char * left, char * right) {
	for (struct TestBinOp * ops = test_binary_ops; ops->name; ops++) {
		if (!strcmp(ops->name, op)) return ops->op(name, left, right);
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
			if (test_is_una(argv[0])) return test_do_una(name, argv[0], argv[1]);
			return fprintf(stderr, "%s: %s: expected unary operator\n", name, argv[0]), 2;
		case 3:
			if (test_is_bin(argv[1])) return test_do_bin(name, argv[1], argv[0], argv[2]);
			if (!strcmp(argv[0],"!")) {
				int res = eval(name, argc - 1, argv + 1);
				if (res == 2) return res;
				return !res;
			}
			return fprintf(stderr, "%s: %s: expected binary operator\n", name, argv[1]), 2;
		case 4:
			if (!strcmp(argv[0],"!")) {
				int res = eval(name, argc - 1, argv + 1);
				if (res == 2) return res;
				return !res;
			}
			break;
	}
	return fprintf(stderr, "%s: too many arguments\n", name), 2;
}

#ifdef TEST_IS_ESH
uint32_t shell_cmd_test(int argc, char * argv[]) {
#else
int main(int argc, char * argv[]) {
#endif
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
