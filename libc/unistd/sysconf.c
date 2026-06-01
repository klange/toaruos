#include <libc/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/signal_defs.h>

long sysconf(int name) {
	if (name < 0 || name >= __SC_MAX) {
		errno = EINVAL;
		return -1;
	}

	switch (name) {
		/* Various things where we haven't defined limits. */
		case _SC_ARG_MAX:
		case _SC_OPEN_MAX:
		case _SC_THREAD_THREADS_MAX:
			return -1;

		case _SC_NGROUPS_MAX:
			return 32; /* defined in kernel */

		case _SC_CLK_TCK:
			return CLOCKS_PER_SEC;

		case _SC_JOB_CONTROL:
			return 1; /* Should be _POSIX_JOB_CONTROL */

		case _SC_HOST_NAME_MAX:
			return 255;

		case _SC_ATEXIT_MAX:
			return ATEXIT_MAX;

		case _SC_SYMLOOP_MAX:
			return 8; /* should be SYMLOOP_MAX */

		case _SC_PAGESIZE:
			return 4096;

		case _SC_NSIG:
			return NUMSIGNALS;

		case _SC_V7_LP64_OFF64:
		case _SC_V8_LP64_OFF64:
			return 1;

		case _SC_TTY_NAME_MAX:
			return NAME_MAX;

		case _SC_NPROCESSORS_CONF:
		case _SC_NPROCESSORS_ONLN:
			return syscall_nproc();

		default:
			/* For everything else, we return -1 without setting errno. */
			return -1;
	}
}
