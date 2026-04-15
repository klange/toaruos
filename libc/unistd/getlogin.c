#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static char _name[64]; /* NAME_MAX ? */

int getlogin_r(char * buf, size_t bufsize) {
	int tty = STDIN_FILENO;
	if (!isatty(tty)) {
		tty = STDOUT_FILENO;
		if (!isatty(tty)) {
			tty = STDERR_FILENO;
			if (!isatty(tty)) {
				errno = ENOTTY;
				return -1;
			}
		}
	}

	/* Get the owner */
	struct stat statbuf;
	if (fstat(tty, &statbuf) == -1) return -1;

	struct passwd * passwd = getpwuid(statbuf.st_uid);
	if (!passwd) return -1;
	if (!passwd->pw_name) return -1;
	if ((unsigned int)snprintf(buf, bufsize, passwd->pw_name) >= bufsize) return (errno = ERANGE), -1;

	return 0;
}

char * getlogin(void) {
	if (getlogin_r(_name, 64) == -1) return NULL;
	return _name;
}
