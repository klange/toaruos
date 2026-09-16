#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void sig_int(int sig, siginfo_t * info, void * ctx) {
	int val_on_stack = sig;
	dprintf(STDERR_FILENO, "addr = %p\n", &val_on_stack);
}

int main(int argc, char * argv[]) {
	struct sigaction sa = {0};
	sa.sa_sigaction = sig_int;
	sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
	sigaction(SIGINT, &sa, NULL);

	stack_t new_stack;
	new_stack.ss_sp = malloc(10240);
	new_stack.ss_size = 10240;
	new_stack.ss_flags = 0;

	stack_t old_stack;

	sigaltstack(&new_stack, &old_stack);

	fprintf(stderr, "new_stack = {%p, %zu, %u}\n",
		new_stack.ss_sp, new_stack.ss_size, new_stack.ss_flags);

	fprintf(stderr, "old_stack = {%p, %zu, %u}\n",
		old_stack.ss_sp, old_stack.ss_size, old_stack.ss_flags);

	usleep(1000 * 1000 * 10);

	return 0;
}
