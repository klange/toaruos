#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include "internal.h"
#include "pthread/internal.h"
#include "stdio/stdio_internal.h"

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(exit,  SYS_EXT, int);

extern void _init();
extern void _fini();

char ** environ = NULL;
int __environ_size = 0;

_hidden int __libc_debug = 0;

/* This is exported. */
char ** __argv = NULL;

static char ** __get_argv(void) {
	return __argv;
}

void _exit(int val){
	_fini();
	__stdio_cleanup();
	syscall_exit(val);
	__builtin_unreachable();
}

int __libc_is_multicore = 0; /* exported */

static int __libc_init_called = 0;

__attribute__((constructor))
static void _libc_init(void) {
	__libc_init_called = 1;
	__make_tls();
	__stdio_init_buffers();
	__libc_is_multicore = syscall_nproc();

	unsigned int x = 0;
	unsigned int nulls = 0;
	for (x = 0; 1; ++x) {
		if (!__get_argv()[x]) {
			++nulls;
			if (nulls == 2) {
				break;
			}
			continue;
		}
		if (nulls == 1) {
			environ = &__get_argv()[x];
			break;
		}
	}
	if (!environ) {
		environ = malloc(sizeof(char *) * 4);
		environ[0] = NULL;
		environ[1] = NULL;
		environ[2] = NULL;
		environ[3] = NULL;
		__environ_size = 4;
	} else {
		/* Find actual size */
		int size = 0;

		char ** tmp = environ;
		while (*tmp) {
			size++;
			tmp++;
		}

		if (size < 4) {
			__environ_size = 4;
		} else {
			/* Multiply by two */
			__environ_size = size * 2;
		}

		char ** new_environ = malloc(sizeof(char*) * __environ_size);
		int i = 0;
		while (i < __environ_size && environ[i]) {
			new_environ[i] = environ[i];
			i++;
		}

		while (i < __environ_size) {
			new_environ[i] = NULL;
			i++;
		}

		environ = new_environ;
	}
	if (getenv("__LIBC_DEBUG")) __libc_debug = 1;
}

void pre_main(int argc, char * argv[], char ** envp, int (*main)(int,char**)) {
	_init();
	exit(main(argc, argv));
}

