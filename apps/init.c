#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <wait.h>
#include <sys/wait.h>

#define INITD_PATH "/etc/startup.d"

void set_console(void) {
	int _stdin  = syscall_open("/dev/ttyS0", 0, 0);
	int _stdout = syscall_open("/dev/ttyS0", 1, 0);
	int _stderr = syscall_open("/dev/ttyS0", 1, 0);

	if (_stdout < 0) {
		_stdout = syscall_open("/dev/null", 1, 0);
		_stderr = syscall_open("/dev/null", 1, 0);
	}

	(void)_stderr;
	(void)_stdin;
}

int start_options(char * args[]) {
	int cpid = syscall_fork();
	if (!cpid) {
		char * _envp[] = {
			"LD_LIBRARY_PATH=/lib:/usr/lib",
			"PATH=/bin:/usr/bin",
			"USER=root",
			"HOME=/home/root",
			NULL,
		};
		syscall_execve(args[0], args, _envp);
		syscall_exit(0);
	}

	int pid = 0;
	do {
		pid = waitpid(-1, NULL, WNOKERN);
		if (pid == -1 && errno == ECHILD) {
			break;
		}
		if (pid == cpid) {
			break;
		}
	} while ((pid > 0) || (pid == -1 && errno == EINTR));

	return cpid;
}

int main(int argc, char * argv[]) {
	/* Initialize stdin/out/err */
	set_console();

	/* Get directory listing for /etc/startup.d */
	int initd_dir = syscall_open(INITD_PATH, 0, 0);
	if (initd_dir < 0) {
		/* No init scripts; try to start getty as a fallback */
		start_options((char *[]){"/bin/getty",NULL});
	} else {
		int count = 0, i = 0, ret = 0;

		/* Figure out how many entries we have with a dry run */
		do {
			struct dirent ent;
			ret = syscall_readdir(initd_dir, ++count, &ent);
		} while (ret > 0);

		/* Read each directory entry */
		struct dirent entries[count];
		do {
			syscall_readdir(initd_dir, i, &entries[i]);
			i++;
		} while (i < count);

		/* Sort the directory entries */
		int comparator(const void * c1, const void * c2) {
			const struct dirent * d1 = c1;
			const struct dirent * d2 = c2;
			return strcmp(d1->d_name, d2->d_name);
		}
		qsort(entries, count, sizeof(struct dirent), comparator);

		/* Run scripts */
		for (int i = 0; i < count; ++i) {
			if (entries[i].d_name[0] != '.') {
				char path[256];
				sprintf(path, "/etc/startup.d/%s", entries[i].d_name);
				start_options((char *[]){path, NULL});
			}
		}
	}

	syscall_reboot();
	return 0;
}
