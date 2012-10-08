#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	/* Starts a graphical session and then spins waiting for a kill (logout) signal */

	int _wallpaper_pid = fork();
	if (!_wallpaper_pid) {
		char * args[] = {"/bin/wallpaper", NULL};
		execvp(args[0], args);
	}
	int _panel_pid = fork();
	if (!_panel_pid) {
		char * args[] = {"/bin/panel", NULL};
		execvp(args[0], args);
	}

	syscall_wait(_panel_pid);

	printf("Session leader has exited. Sending INT signals to %d.\n", _wallpaper_pid);

	syscall_send_signal(_wallpaper_pid, 2);

	printf("Waiting on wallpaper.\n");
	syscall_wait(_wallpaper_pid);

	printf("Session has ended.\n");

}
