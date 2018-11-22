#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	if (argc < 2 ){
		fprintf(stderr, "usage: test-lock LOCKPATH\n");
		return 1;
	}
	int fd = open(argv[1],O_RDWR|O_CREAT|O_EXCL);
	if (fd < 0) {
		if (errno == EEXIST) {
			fprintf(stderr, "Lock is already held.\n");
			return 0;
		} else {
			fprintf(stderr, "Some other error? %d = %s\n", errno, strerror(errno));
			return 1;
		}
	} else {
		fprintf(stderr, "I have the lock, the fd is %d.\n", fd);
		fprintf(stderr, "Press Enter to release lock.\n");
		while (!feof(stdin) && fgetc(stdin) != '\n') {
			/* nothing */
		}
		close(fd);
		unlink(argv[1]);
		return 0;
	}
}
