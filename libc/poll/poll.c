#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <sys/fswait.h>

extern char * _argv_0;

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
	int count_pollin = 0;

	for (nfds_t i = 0; i < nfds; ++i) {
		if (fds[i].events & POLLIN) {
			count_pollin++;
		}
		fds[i].revents = 0;
	}

	for (nfds_t i = 0; i < nfds; ++i) {
		if (fds[i].events & (~POLLIN)) {
			fprintf(stderr, "%s: poll: unsupported bit set in fds (this implementation only supports POLLIN)\n", _argv_0);
			return -EINVAL;
		}
	}

	int fswait_fds[count_pollin];
	int fswait_backref[count_pollin];
	int j = 0;
	for (nfds_t i = 0; i < nfds; ++i) {
		if (fds[i].events & POLLIN) {
			fswait_fds[j] = fds[i].fd;
			fswait_backref[j] = i;
			j++;
		}
	}

	int ret = fswait2(count_pollin, fswait_fds, timeout);

	if (ret >= 0 && ret < count_pollin) {
		fds[fswait_backref[ret]].revents = POLLIN;
		return 1;
	} else if (ret == count_pollin) {
		return 0;
	} else {
		return ret; /* Error */
	}
}
