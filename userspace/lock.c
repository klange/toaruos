#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <syscall.h>

DEFN_SYSCALL2(signal, 38, uint32_t, void *);
DEFN_SYSCALL2(send_signal, 37, uint32_t, uint32_t)

void sig_int(int sig) {
	/* Ignore */
}

void main(int argc, char * argv[]) {
	syscall_signal(2, sig_int);

	char * password_a = malloc(sizeof(char) * 1024);
	char * password_b = malloc(sizeof(char) * 1024);

	printf("\033[H\033[2J");

	fprintf(stdout, "Enter a lock password: \033[1001z");
	fflush(stdout);
	fgets(password_a, 1024, stdin);
	password_a[strlen(password_a)-1] = '\0';
	fprintf(stdout, "\033[1002z\n");

	uint32_t failures = 0;

	do {
		printf("\033[H\033[2J");
		if (failures > 0) {
			printf("\n\033[1;41;33mIncorrect password. (%d failure%s)\033[0m\n", failures, (failures > 1 ? "s" : ""));
		}
		printf("\n\033[1;31mSystem is locked.\033[0m\n\n");
		fprintf(stdout, "Enter password to unlock: \033[1001z");
		fflush(stdout);
		fgets(password_b, 1024, stdin);
		password_b[strlen(password_b)-1] = '\0';
		fprintf(stdout, "\033[1002z\n");
		failures++;
	} while (strcmp(password_a, password_b) != 0);
}
