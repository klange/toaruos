#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	while (1) {
		pid_t f = fork();
		if (!f) {
			char * args[] = {
				"login",
				NULL
			};
			execvp(args[0], args);
		} else {
			int result;
			do {
				result = waitpid(f, NULL, 0);
			} while (result < 0);
		}
	}

	return 1;
}
