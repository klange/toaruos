#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static char _name[64]; /* NAME_MAX ? */

char * getlogin(void) {

	int tty = STDIN_FILENO;
	if (!isatty(tty)) {
		tty = STDOUT_FILENO;
		if (!isatty(tty)) {
			tty = STDERR_FILENO;
			if (!isatty(tty)) {
				errno = ENOTTY;
				return NULL;
			}
		}
	}

	char * name = ttyname(tty);
	if (!name) return NULL;

	/* Get the owner */
	struct stat statbuf;
	stat(name, &statbuf);

	struct passwd * passwd = getpwuid(statbuf.st_uid);

	if (!passwd) return NULL;
	if (!passwd->pw_name) return NULL;

	memcpy(_name, passwd->pw_name, strlen(passwd->pw_name));
	return _name;
}
