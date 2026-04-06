#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <sys/sysfunc.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uregs.h>
#include <syscall_nums.h>

int main(int argc, char * argv[]) {
	pid_t p = fork();
	if (!p) {
		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
		execvp(argv[optind], &argv[optind]);
		return 1;
	}

	while (1) {
		int status = 0;
		pid_t res = waitpid(p, &status, WSTOPPED);

		if (res < 0) {
			fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
		} else {
			if (WIFSTOPPED(status)) {
				if (WSTOPSIG(status) == SIGTRAP) {
					struct URegs regs;
					ptrace(PTRACE_GETREGS, p, NULL, &regs);

					/* Event type */
					int event = (status >> 16) & 0xFF;
					switch (event) {
						case PTRACE_EVENT_SYSCALL_ENTER:
							if (uregs_syscall_num(&regs) == SYS_SLEEP) {
								fprintf(stderr, "%s: sleep called, rewriting to yield\n", argv[0]);
								uregs_syscall_num(&regs) = SYS_YIELD;
								ptrace(PTRACE_SETREGS, p, NULL, &regs);
							}
							break;
						default:
							break;
					}
					ptrace(PTRACE_CONT, p, NULL, NULL);
				} else {
					ptrace(PTRACE_CONT, p, NULL, (void*)(uintptr_t)WSTOPSIG(status));
				}
			} else if (WIFSIGNALED(status)) {
				return 0;
			} else if (WIFEXITED(status)) {
				return 0;
			}
		}
	}

	return 0;
}

