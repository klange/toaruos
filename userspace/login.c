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

DEFN_SYSCALL1(wait, 17, unsigned int);
DEFN_SYSCALL1(setuid, 24, unsigned int);
DEFN_SYSCALL1(kernel_string_XXX, 25, char *);

typedef struct {
	int uid;
	char * name;
	char * pass;
} user_combo;

user_combo users[] = {
	{0, "root", "toor"},
	{1, "klange", "herp"}
};

int n_users = 2;

int readline(char * buf, size_t size, uint8_t display) {
	size_t collected = 0;
	while (collected < size - 1) {
		char * cmd = malloc(2);
		size_t nread = fread(cmd, 1, 1, stdin);
		if (nread > 0) {
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
	for (int i = 0; i < n_users; ++i) {
		if (!strcmp(user, users[i].name)) {
			if (!strcmp(pass, users[i].pass)) {
				return users[i].uid;
			}
		}
	}
	return -1;
}

int main(int argc, char ** argv) {
	/* TODO: Read /etc/shadow */

	/* TODO: uname() */
	char * _uname = malloc(sizeof(char) * 1024);
	syscall_kernel_string_XXX(_uname);

	fprintf(stdout, "\n%s\n\n", _uname);

	while (1) {
		char * username = malloc(sizeof(char) * 1024);
		char * password = malloc(sizeof(char) * 1024);

		/* TODO: gethostname() */
		char * _hostname = "test";

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
