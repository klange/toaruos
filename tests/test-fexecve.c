#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

extern char **environ;

int main(int argc, char * argv[]) {
	int fd = open("/bin/ls", O_PATH);

	char * args[] = {"ls", "-l", NULL};

	int ret = fexecve(fd, args, environ);

	err(1, "fexecve: %d", ret);

	return 1;
}
