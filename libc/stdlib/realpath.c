#define _TOARU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

static size_t count_slashes(const char * s) {
	size_t i = 0;
	while (s[i] == '/') i++;
	return i;
}

static char * set_errno(int e) {
	errno = e;
	return NULL;
}

/**
 * Mostly copied from musl because I simply can't be bothered any more.
 *
 * Copyright © 2005-2020 Rich Felker, et al.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
char * __realpath(const char * restrict filename, char * restrict resolved, int messy) {
	if (!filename) return set_errno(EINVAL);

	char stack[PATH_MAX + 1];
	char out[PATH_MAX];

	size_t sym_max = sysconf(_SC_SYMLOOP_MAX);

	size_t len = strlen(filename);
	if (len == 0) return set_errno(ENOENT);
	if (len > PATH_MAX) return set_errno(ENAMETOOLONG);

	size_t a = sizeof(stack) - len - 1;
	size_t b = 0;
	size_t c = 0;
	size_t symcnt = 0;
	size_t ups = 0;
	int    check_dir = 0;
	int    bad_entry = 0;

	memcpy(stack + a, filename, len + 1);

_retry:
	for (;; a += count_slashes(stack + a)) {
		if (stack[a] == '/') {
			/* Reset state */
			check_dir = ups = b = 0;
			out[b++] = '/';
			a++;
			if (stack[a] == '/' && stack[a+1] != '/') out[b++] = '/';
			continue;
		}

		char * next_slash = strchrnul(stack + a, '/');
		c = len = next_slash - (stack + a);

		if (!len && !check_dir) break;

		if (len == 1 && stack[a] == '.') {
			a += len;
			continue;
		}

		if (b && out[b-1] != '/') {
			if (!a) return set_errno(ENAMETOOLONG);
			stack[--a] = '/';
			len++;
		}

		if (b + len >= PATH_MAX) return set_errno(ENAMETOOLONG);

		memcpy(out + b, stack + a, len);
		out[b + len] = '\0';
		a += len;

		int up = 0;
		if (c == 2 && stack[a-2] == '.' && stack[a-1] == '.') {
			up = 1;
			if (b <= ups * 3) {
				ups++;
				b += len;
				continue;
			}

			if (!check_dir) goto _no_readlink;
		}

		if (bad_entry) return set_errno(ENOENT);

		ssize_t link_len = readlink(out, stack, a);
		if ((size_t)link_len == a)  return set_errno(ENAMETOOLONG);
		if (!link_len) return set_errno(ENOENT);

		if (link_len < 0) {
			if (errno != EINVAL && (errno != ENOENT || !messy)) return NULL; /* Something other than not a symlink */
			if (errno == ENOENT) bad_entry = 1;
			/* Not a symlink */
_no_readlink: (void)0;

			check_dir = 0;

			if (up) {
				while (b && out[b-1] != '/') b--;
				if (b > 1 && (b > 2 || *out != '/')) b--;
				continue;
			}

			if (c) b += len;
			check_dir = stack[a];
			continue;
		}

		if (++symcnt > sym_max) return set_errno(ELOOP);

		if (stack[link_len - 1] == '/') while (stack[a] == '/') a++;
		a -= link_len;
		memmove(stack + a, stack, link_len);

		goto _retry;
	}

	out[b] = '\0';
	if (*out != '/') {
		if (!getcwd(stack, sizeof(stack))) return NULL;
		len = strlen(stack);

		a = 0;
		while (ups--) {
			while (len > 1 && stack[len - 1] != '/') len--;
			if (len > 1) len--;
			a += 2;
			if (a < b) a++;
		}

		if (b - a && stack[len - 1] != '/') stack[len++] = '/';

		if (len + (b - a) + 1 >= PATH_MAX) return set_errno(ENAMETOOLONG);

		memmove(out + len, out + a, b - a + 1);
		memcpy(out, stack, len);
		b = len + b - a;
	}

	if (resolved) return memcpy(resolved, out, b + 1);

	return strdup(out);
}

char * realpath(const char * restrict filename, char * restrict resolved) {
	return __realpath(filename, resolved, 0);
}
