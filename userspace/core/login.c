/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Login Service
 *
 * Provides the user with a login prompt and starts their session.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <sys/utsname.h>

#include "lib/sha2.h"

uint32_t child = 0;

void sig_int(int sig) {
	/* Pass onto the shell */
	if (child) {
		syscall_send_signal(child, sig);
	}
	/* Else, ignore */
}

void sig_segv(int sig) {
	printf("Segmentation fault.\n");
	exit(127 + sig);
	/* no return */
}

int checkUserPass(char * user, char * pass) {

	/* Generate SHA512 */
	char hash[SHA512_DIGEST_STRING_LENGTH];
	SHA512_Data(pass, strlen(pass), hash);

	/* Open up /etc/master.passwd */

	FILE * passwd = fopen("/etc/master.passwd", "r");
	char line[2048];

	while (fgets(line, 2048, passwd) != NULL) {

		line[strlen(line)-1] = '\0';

		char *p, *tokens[4], *last;
		int i = 0;
		for ((p = strtok_r(line, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;
		
		if (strcmp(tokens[0],user) != 0) {
			continue;
		}
		if (!strcmp(tokens[1],hash)) {
			fclose(passwd);
			return atoi(tokens[2]);
		}
		}
	fclose(passwd);
	return -1;

}

int main(int argc, char ** argv) {

	struct utsname u;
	uname(&u);

	fprintf(stdout, "\n%s %s %s %s\n\n",
			u.sysname,
			u.nodename,
			u.release,
			u.version
			);

	syscall_signal(2, sig_int);
	syscall_signal(11, sig_segv);

	while (1) {
		char * username = malloc(sizeof(char) * 1024);
		char * password = malloc(sizeof(char) * 1024);

		/* TODO: gethostname() */
		char _hostname[256];
		syscall_gethostname(_hostname);

		fprintf(stdout, "%s login: ", _hostname);
		fflush(stdout);
		fgets(username, 1024, stdin);
		username[strlen(username)-1] = '\0';

		fprintf(stdout, "password: \033[1001z");
		fflush(stdout);
		fgets(password, 1024, stdin);
		password[strlen(password)-1] = '\0';
		fprintf(stdout, "\033[1002z\n");

		int uid = checkUserPass(username, password);

		if (uid < 0) {
			fprintf(stdout, "\nLogin failed.\n");
			continue;
		}

		pid_t pid = getpid();

		uint32_t f = fork();
		if (getpid() != pid) {
			/* TODO: Read appropriate shell from /etc/passwd */
			char * args[] = {
				"/bin/esh",
				NULL
			};
			syscall_setuid(uid);
			int i = execvp(args[0], args);
		} else {
			child = f;
			syscall_wait(f);
		}
		child = 0;
		free(username);
		free(password);
	}

	return 0;
}
