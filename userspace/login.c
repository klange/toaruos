/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Login Service
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "lib/sha2.h"

DEFN_SYSCALL1(wait, 17, unsigned int);
DEFN_SYSCALL1(setuid, 24, unsigned int);
DEFN_SYSCALL1(kernel_string_XXX, 25, char *);
DEFN_SYSCALL0(gethostname, 32);

int readline(char * buf, size_t size, uint8_t display) {
	size_t collected = 0;
	char * cmd = malloc(2);
	while (collected < size - 1) {
		size_t nread = fread(cmd, 1, 1, stdin);
		if (nread > 0) {
			if (cmd[0] == 8) {
				/* Backspace */
				if (collected > 0) {
					collected--;
					buf[collected] = '\0';
					if (display || buf[collected] == '\n') {
						printf("%c", cmd[0]);
					}
					fflush(stdout);
				}
				continue;
			}
			if (cmd[0] < 10 || (cmd[0] > 10 && cmd[0] < 32) || cmd[0] > 126) {
				continue;
			}
			buf[collected] = cmd[0];
			if (display || buf[collected] == '\n') {
				printf("%c", cmd[0]);
			}
			fflush(stdout);
			if (buf[collected] == '\n') {
				goto _done;
			}
			collected++;
		}
	}
_done:
	buf[collected] = '\0';
	return collected;
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

	/* TODO: uname() */
	char * _uname = malloc(sizeof(char) * 1024);
	syscall_kernel_string_XXX(_uname);

	fprintf(stdout, "\n%s\n\n", _uname);

	while (1) {
		char * username = malloc(sizeof(char) * 1024);
		char * password = malloc(sizeof(char) * 1024);

		/* TODO: gethostname() */
		char _hostname[256];
		syscall_gethostname(_hostname);

		fprintf(stdout, "%s login: ", _hostname);
		fflush(stdout);
		readline(username, 1024, 1);

		fprintf(stdout, "password: ");
		fflush(stdout);
		readline(password, 1024, 0);

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
			int i = execve(args[0], args, NULL);
		} else {
			syscall_wait(f);
		}
		free(username);
		free(password);
	}

	return 0;
}
