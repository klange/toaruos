#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>

#define LINE_LEN 1024

void set_username() {
	FILE * passwd = fopen("/etc/passwd", "r");
	char line[LINE_LEN];
	
	int uid = syscall_getuid();

	while (fgets(line, LINE_LEN, passwd) != NULL) {

		line[strlen(line)-1] = '\0';

		char *p, *tokens[10], *last;
		int i = 0;
		for ((p = strtok_r(line, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;

		if (atoi(tokens[2]) == uid) {
			setenv("USER", tokens[0], 1);
		}
	}
	fclose(passwd);
}

void set_homedir() {
	char * user = getenv("USER");
	if (user) {
		char path[512];
		sprintf(path,"/home/%s", user);
		setenv("HOME",path,1);
	} else {
		setenv("HOME","/",1);
	}
}

void set_path() {
	setenv("PATH", "/bin", 0);
}

int main(int argc, char * argv[]) {
	/* Starts a graphical session and then spins waiting for a kill (logout) signal */

	/* Load some session variables */
	set_username();
	set_homedir();
	set_path();

	int _wallpaper_pid = fork();
	if (!_wallpaper_pid) {
		char * args[] = {"/bin/wallpaper-beta", NULL};
		execvp(args[0], args);
	}
	int _panel_pid = fork();
	if (!_panel_pid) {
		char * args[] = {"/bin/panel-beta", NULL};
		execvp(args[0], args);
	}
	int _terminal_pid = fork();
	if (!_terminal_pid) {
		char * args[] = {"/bin/terminal-beta", NULL};
		execvp(args[0], args);
	}

	syscall_wait(_panel_pid);

	printf("Session leader has exited. Sending INT signals to %d.\n", _wallpaper_pid);

	kill(_wallpaper_pid, SIGINT);

	printf("Waiting on wallpaper.\n");
	syscall_wait(_wallpaper_pid);

	printf("Session has ended.\n");

}
