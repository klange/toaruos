/*
 * cat
 *
 * Concatenates files together to standard output.
 * In a supporting terminal, you can then pipe
 * standard out to another file or other useful
 * things like that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>
#include "lib/pthread.h"
#include "../kernel/include/signal.h"

DEFN_SYSCALL1(serial,  44, int);
int fd = 0;

int child_pid = 0;

void *print_serial_stuff(void * garbage) {
	child_pid = gettid();
	while (1) {
		char buf[1024];
		size_t size = read(fd, buf, 1024);

		for (int i = 0; i < size; ++i) {
			char x = buf[i];
			if (x == 13) continue;
			fputc(x, stdout);
		}
		if (size) fflush(stdout);
	}

	pthread_exit(garbage);
}

int main(int argc, char ** argv) {
	int device = 0x2F8;
	pthread_t receive_thread;
	pthread_t flush_thread;

	if (argc > 1) {
		if (!strcmp(argv[1], "com1")) {
			device = 0x3F8;
		} else if (!strcmp(argv[1], "com2")) {
			device = 0x2F8;
		} else {
			fprintf(stderr, "Unrecognized com device, try com1 or com2; default is com2.\n");
			return 1;
		}
	}

	printf("\033[1560z");
	fflush(stdout);

	fd = syscall_serial(device);
	pthread_create(&receive_thread, NULL, print_serial_stuff, NULL);

	while (1) {
		char c = fgetc(stdin);
		if (c == 27) {
			char x = fgetc(stdin);
			if (x == ']') {
				while (1) {
					printf("serial-console>\033[1561z ");
					fflush(stdout);

					char line[1024];
					fgets(line, 1024, stdin);

					int i = strlen(line);
					line[i-1] = '\0';

					if (!strcmp(line, "quit")) {
						syscall_send_signal(child_pid, SIGKILL);
						printf("Waiting for threads to shut down...\n");
						syscall_wait(child_pid);

						printf("Exiting.\n");
						return 0;
					} else if (!strcmp(line, "continue")) {
						printf("\033[1560z");
						fflush(stdout);
						break;
					}
				}
			} else {
				ungetc(x, stdin);
			}
		}
		char buf[1] = {c};
		write(fd, buf, 1);
	}

	close(fd);
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
