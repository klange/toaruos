/*
 * E-Shell
 *
 * Test shell for ToAruOS
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL1(wait, 17, unsigned int);

int main(int argc, char ** argv) {
	printf("I am pid %d\n", getpid());
	char cwd[1024] = {'/',0};
	int  pid = getpid();
	while (1) {
		char cmd[1024];
		printf("%s$ ", cwd);
		fflush(stdout);
		fgets(cmd, 1024, stdin);
		cmd[strlen(cmd)-1] = '\0';
		char *p, *tokens[512], *last;
		int i = 0;
		for ((p = strtok_r(cmd, " ", &last)); p;
				(p = strtok_r(NULL, " ", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;
		if (!tokens[0] || strlen(tokens[0]) < 1) {
			continue;
		}
		if (!strcmp(tokens[0],"exit")) {
			goto exit;
		}
		uint32_t f = fork();
		if (getpid() != pid) {
			int i = execve(tokens[0], tokens, NULL);
			return i;
		} else {
			int i = syscall_wait(f);
			if (i) {
				printf("[%d] ");
			}
		}
	}
exit:

	return 0;
}
