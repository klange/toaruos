/* vim: tabstop=4 shiftwidth=4 noexpandtab
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
#include <time.h>

DEFN_SYSCALL1(wait, 17, unsigned int);

char cwd[1024] = {'/',0};
struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};


void draw_prompt() {
	struct tm * timeinfo;
	struct timeval now;
	syscall_gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	char date_buffer[80];
	strftime(date_buffer, 80, "%m/%d", timeinfo);
	char time_buffer[80];
	strftime(time_buffer, 80, "%H:%M:%S", timeinfo);
	printf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%s \033[1;34m%s\033[0m \033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ",
			"test", "esh", date_buffer, time_buffer, cwd);
	fflush(stdout);
}

int readline(char * buf, size_t size) {
	size_t collected = 0;
	while (collected < size - 1) {
		char * cmd = malloc(2);
		size_t nread = fread(cmd, 1, 1, stdin);
		if (nread > 0) {
			buf[collected] = cmd[0];
			printf("%c", cmd[0]);
			fflush(stdout);
			if (buf[collected] == '\n') {
				collected++;
				goto _done;
			}
			collected++;
		}
	}
_done:
	buf[collected] = '\0';
	return collected;
}

int main(int argc, char ** argv) {
	printf("I am pid %d\n", getpid());

	int  pid = getpid();
	int  nowait = 0;
	int  free_cmd = 0;
	while (1) {
		char * cmd = malloc(sizeof(char) * 1024);

		draw_prompt();
#if 0
		fgets(cmd, 1024, stdin);
#endif
		readline(cmd, 1024);
		cmd[strlen(cmd)-1] = '\0';
		char *p, *tokens[512], *last;
		int i = 0;
		for ((p = strtok_r(cmd, " ", &last)); p;
				(p = strtok_r(NULL, " ", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;
		if (!tokens[0] || strlen(tokens[0]) < 1) {
			free(cmd);
			continue;
		}
		if (!strcmp(tokens[0],"exit")) {
			goto exit;
		}
		nowait = (!strcmp(tokens[i-1],"&"));

		/* Attempt to open the command */
		FILE * file = fopen(tokens[0], "r");
		if (!file) {
			if (!strstr(tokens[0],"/")) {
				cmd = malloc(sizeof(char) * (strlen(tokens[0]) + strlen("/bin/") + 1));
				sprintf(cmd, "%s%s", "/bin/", tokens[0]);
				file = fopen(cmd,"r");
				if (!file) {
					printf("Command not found: %s\n", tokens[0]);
					free(cmd);
					continue;
				}
				fclose(file);
			} else {
				printf("Command not found: %s\n", tokens[0]);
				free(cmd);
				continue;
			}
		} else {
			fclose(file);
		}


		uint32_t f = fork();
		if (getpid() != pid) {
			int i = execve(cmd, tokens, NULL);
			return i;
		} else {
			if (!nowait) {
				int i = syscall_wait(f);
				if (i) {
					printf("[%d] ");
				}
			}
			free(cmd);
		}
	}
exit:

	return 0;
}
