#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(exit,  SYS_EXT, int);
DEFN_SYSCALL2(sleepabs,  SYS_SLEEPABS, unsigned long, unsigned long);

extern void _init();
extern void _fini();

char ** environ = NULL;
int _environ_size = 0;
char * _argv_0 = NULL;
int __libc_debug = 0;

char ** __argv = NULL;
extern char ** __get_argv(void) {
	return __argv;
}

extern void __stdio_init_buffers(void);

void _exit(int val){
	_fini();
	syscall_exit(val);

	__builtin_unreachable();
}

__attribute__((constructor))
static void _libc_init(void) {
	__stdio_init_buffers();

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
		_environ_size = 4;
	} else {
		/* Find actual size */
		int size = 0;

		char ** tmp = environ;
		while (*tmp) {
			size++;
			tmp++;
		}

		if (size < 4) {
			_environ_size = 4;
		} else {
			/* Multiply by two */
			_environ_size = size * 2;
		}

		char ** new_environ = malloc(sizeof(char*) * _environ_size);
		int i = 0;
		while (i < _environ_size && environ[i]) {
			new_environ[i] = environ[i];
			i++;
		}

		while (i < _environ_size) {
			new_environ[i] = NULL;
			i++;
		}

		environ = new_environ;
	}
	if (getenv("__LIBC_DEBUG")) __libc_debug = 1;
	_argv_0 = __get_argv()[0];
}

void pre_main(int (*main)(int,char**), int argc, char * argv[]) {
	if (!__get_argv()) {
		/* Statically loaded, must set __argv so __get_argv() works */
		__argv = argv;
	}
	_init();
	exit(main(argc, argv));
}

