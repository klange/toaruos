#include <signal.h>
#include <unistd.h>

int raise(int sig) {
	return kill(getpid(), sig);
}
