#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	/* Starts a graphical session and then spins waiting for a kill (logout) signal */

	int _wallpaper_pid = fork();
	if (!_wallpaper_pid) {
		char * args[] = {"/bin/wallpaper", NULL};
		execve(args[0], args, NULL);
	}
	int _panel_pid = fork();
	if (!_panel_pid) {
		char * args[] = {"/bin/panel", NULL};
		execve(args[0], args, NULL);
	}
	int _terminal_pid = fork();
	if (!_terminal_pid) {
		char * args[] = {"/bin/terminal", NULL};
		execve(args[0], args, NULL);
	}

	syscall_wait(_terminal_pid);

	printf("Terminal has exited. Sending kill signals to %d and %d.\n", _wallpaper_pid,  _panel_pid);

	syscall_send_signal(_wallpaper_pid, 2);
	syscall_send_signal(_panel_pid,     2);

	printf("Waiting on wallpaper.\n");
	syscall_wait(_wallpaper_pid);

	printf("Waiting on panel.\n");
	syscall_wait(_panel_pid);

}
