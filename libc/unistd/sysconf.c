#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>
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

		case _SC_PAGESIZE:
			return 4096;

		case _SC_NSIG:
			return NUMSIGNALS;

		case _SC_TTY_NAME_MAX:
			return NAME_MAX;

		case _SC_NPROCESSORS_CONF:
		case _SC_NPROCESSORS_ONLN:
			return syscall_nproc();

		default:
			errno = -ENOSYS;
			return -1;
	}
}
