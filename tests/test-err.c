#include <err.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "wWeE")) != -1) {
		switch (opt) {
			case 'w':
				errno = EINVAL;
				warn("message %d", 123);
				break;
			case 'W':
				errno = EINVAL;
				warnx("message %d", 123);
				break;
			case 'e':
				errno = EINVAL;
				err(42, "message %d", 123);
				break;
			case 'E':
				errno = EINVAL;
				errx(42, "message %d", 123);
				break;
		}
	}

	return 0;
}
