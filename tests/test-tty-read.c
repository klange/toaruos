#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

struct termios old;
struct termios new;
void get_initial_termios(void) {
	tcgetattr(STDIN_FILENO, &old);
}

void on_sigusr1(int sig) {
	fprintf(stderr, "received SIGUSR1\n");
}

int action = TCSADRAIN;

void set_unbuffered(void) {
	memcpy(&new,&old,sizeof(struct termios));
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO) & (~ISIG);
#ifdef VLNEXT
	new.c_cc[VLNEXT] = 0;
#endif
	new.c_cc[VMIN] = 2;
	tcsetattr(STDIN_FILENO, action, &new);
}

void set_buffered(void) {
	tcsetattr(STDIN_FILENO, action, &old);
}

int main(int argc, char * argv[]) {

	if (argc > 1 && !strcmp(argv[1], "flush")) {
		action = TCSAFLUSH;
	}

	get_initial_termios();
	fprintf(stderr, "was VMIN=%d, VTIME=%d\n", old.c_cc[VMIN], old.c_cc[VTIME]);
	set_unbuffered();
	fprintf(stderr, "now VMIN=%d, VTIME=%d\n", new.c_cc[VMIN], new.c_cc[VTIME]);

	signal(SIGUSR1, on_sigusr1);

	char buf[4096];
	ssize_t r = read(STDIN_FILENO, buf, 4096);

	fprintf(stderr, "read=%zd\n", r);

	set_buffered();
}
