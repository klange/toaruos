#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <libc/internal.h>
#include <libc/stdio/stdio_internal.h>

#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL1(exit,  SYS_EXT, int);

extern void _init();

char ** environ = NULL;
int __environ_size = 0;

_hidden int __libc_debug = 0;

/* This is exported. */
char ** __argv = NULL;

static char ** __get_argv(void) {
	return __argv;
}

void _exit(int val){
	syscall_exit(val);
	__builtin_unreachable();
}

void _Exit(int val) __attribute__((weak, alias("_exit")));

int __libc_is_multicore = 0; /* exported */

static int __libc_init_called = 0;
_hidden void __libc_init(void) {
	if (__libc_init_called) return;
	__libc_init_called = 1;
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

void __libc_start_main(int argc, char * argv[], char ** envp, int (*main)(int,char**)) {
	_init();
	exit(main(argc, argv));
}

/* This is what we used to call __libc_start_main, and we maintain this weak alias
 * for compatibility with older binaries, as it is referenced in the old startup
 * code that they all use; new binaries are free to define 'pre_main' differently. */
void pre_main(int, char*[], char**, int (*)(int,char*)) __attribute__((weak,alias("__libc_start_main")));
