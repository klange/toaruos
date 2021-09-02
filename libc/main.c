#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include <syscall.h>
#include <syscall_nums.h>
#include <sys/sysfunc.h>

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
extern void __stdio_cleanup(void);

void _exit(int val){
	_fini();
	__stdio_cleanup();
	syscall_exit(val);
	__builtin_unreachable();
}

extern void __make_tls(void);

int __libc_is_multicore = 0;
static int __libc_init_called = 0;

__attribute__((constructor))
static void _libc_init(void) {
	__libc_init_called = 1;
	__make_tls();
	__stdio_init_buffers();
	__libc_is_multicore = sysfunc(TOARU_SYS_FUNC_NPROC, NULL) > 1;

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

void pre_main(int argc, char * argv[], char ** envp, int (*main)(int,char**)) {
	if (!__get_argv()) {
		/* Statically loaded, must set __argv so __get_argv() works */
		__argv = argv;
		/* Run our initializers, because I'm pretty sure the kernel didn't... */
		if (!__libc_init_called) {
			extern uintptr_t __init_array_start;
			extern uintptr_t __init_array_end;
			for (uintptr_t * constructor = &__init_array_start; constructor < &__init_array_end; ++constructor) {
				void (*constr)(void) = (void*)*constructor;
				constr();
			}
		}
	}
	_init();
	exit(main(argc, argv));
}

