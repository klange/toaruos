#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>

volatile int _end_session = 0;

void sig_int(int sig) {
	_end_session = 1;
}


int main(int argc, char * argv[]) {
	/* Starts a graphical session and then spins waiting for a kill (logout) signal */
	syscall_signal(2, sig_int);

	if (!fork()) {
		char * args[] = {"/bin/wallpaper", NULL};
		execve(args[0], args, NULL);
	}
	if (!fork()) {
		char * args[] = {"/bin/panel", NULL};
		execve(args[0], args, NULL);
	}
	if (!fork()) {
		char * args[] = {"/bin/terminal", NULL};
		execve(args[0], args, NULL);
	}



	while (!_end_session) {
		syscall_yield();
	}
}
