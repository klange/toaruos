#include <signal.h>
#include <errno.h>

int killpg(pid_t pgrp, int sig) {
	if (pgrp < 1) {
		errno = ESRCH;
		return -1;
	}

	return kill(-pgrp, sig);
}
