/*
 * runs a proper tty on a serial port
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	int fd_master, fd_slave, fd_serial;
	char * file = "/dev/ttyS0";

	if (getuid() != 0) {
		fprintf(stderr, "%s: only root can do that\n", argv[0]);
		return 1;
	}

	if (!fork()) {

		if (argc > 1) {
			file = argv[1];
		}

		syscall_openpty(&fd_master, &fd_slave, NULL, NULL, NULL);
		fd_serial = open(file, O_RDWR);

		if (!fork()) {
			dup2(fd_slave, 0);
			dup2(fd_slave, 1);
			dup2(fd_slave, 2);

			system("ttysize -q");

			char * tokens[] = {"/bin/login",NULL};
			execvp(tokens[0], tokens);
			exit(1);
		} else {

			int fds[2] = {fd_serial, fd_master};

			while (1) {
				int index = syscall_fswait2(2,fds,200);
				char buf[1024];
				int r;
				switch (index) {
					case 0: /* fd_serial */
						r = read(fd_serial, buf, 1);
						write(fd_master, buf, 1);
						break;
					case 1: /* fd_master */
						r = read(fd_master, buf, 1024);
						write(fd_serial, buf, r);
						break;
					default: /* timeout */
						break;
				}

			}

		}

		return 1;
	}

	return 0;
}
