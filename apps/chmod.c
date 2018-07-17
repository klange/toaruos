/* probably-non-compliant chmod implementation */
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

enum mode_set {
	MODE_SET,
	MODE_ADD,
	MODE_REMOVE,
};

static int calc(int mode, int users) {
	int out = 0;
	if (users & 1) {
		out |= (mode << 6);
	}
	if (users & 2) {
		out |= (mode << 3);
	}
	if (users & 4) {
		out |= (mode << 0);
	}
	return out;
}

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s OCTAL-MODE FILE...\n", argv[0]);
		return 1;
	}

	/* Parse mode */
	int mode = 0;
	enum mode_set mode_set = MODE_SET;
	char * c = argv[1];
	int user_modes = 0;
	int all_users = 7;

	while (*c) {
		switch (*c) {
			case '0':
				c++; /* 0 */
				while (*c >= '0' || *c <= '7') {
					mode *= 8;
					mode += (*c - '0');
					c++;
				}
				break;
			case 'u':
				all_users = 0;
				user_modes |= 1;
				c++;
				break;
			case 'g':
				all_users = 0;
				user_modes |= 2;
				c++;
				break;
			case 'o':
				all_users = 0;
				user_modes |= 4;
				c++;
				break;
			case 'a':
				all_users = 7;
				user_modes = 7;
				c++;
				break;
			case '-':
				mode_set = MODE_REMOVE;
				c++;
				break;
			case '+':
				mode_set = MODE_ADD;
				c++;
				break;
			case '=':
				mode_set = MODE_SET;
				c++;
				break;
			case 'r':
				mode |= calc(S_IROTH, user_modes | all_users);
				c++;
				break;
			case 'w':
				mode |= calc(S_IWOTH, user_modes | all_users);
				c++;
				break;
			case 'x':
				mode |= calc(S_IXOTH, user_modes | all_users);
				c++;
				break;
		}
	}

	int i = 2;
	while (i < argc) {
		int actual_mode = 0;
		struct stat _stat;
		if (stat(argv[i], &_stat) < 0) {
			fprintf(stderr, "%s: %s: error with stat\n", argv[0], argv[i]);
		}

		switch (mode_set) {
			case MODE_SET:
				actual_mode = mode;
				break;
			case MODE_ADD:
				actual_mode = _stat.st_mode | mode;
				break;
			case MODE_REMOVE:
				actual_mode = _stat.st_mode &= ~(mode);
				break;
		}

		if (chmod(argv[i], actual_mode) < 0) {
			fprintf(stderr, "%s: %s: error with chmod\n", argv[0], argv[i]);
			return 1;
		}
		i++;
	}

	return 0;
}
