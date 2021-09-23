/**
 * @brief Debugger.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
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
#include <syscall_nums.h>

#include <toaru/rline.h>

struct regs {
	uintptr_t r15, r14, r13, r12;
	uintptr_t r11, r10, r9, r8;
	uintptr_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uintptr_t int_no, err_code;
	uintptr_t rip, cs, rflags, rsp, ss;
};

static void dump_regs(struct regs * r) {
	fprintf(stdout,
		"  $rip=0x%016lx\n"
		"  $rsi=0x%016lx,$rdi=0x%016lx,$rbp=0x%016lx,$rsp=0x%016lx\n"
		"  $rax=0x%016lx,$rbx=0x%016lx,$rcx=0x%016lx,$rdx=0x%016lx\n"
		"  $r8= 0x%016lx,$r9= 0x%016lx,$r10=0x%016lx,$r11=0x%016lx\n"
		"  $r12=0x%016lx,$r13=0x%016lx,$r14=0x%016lx,$r15=0x%016lx\n"
		"  cs=0x%016lx  ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->rip,
		r->rsi, r->rdi, r->rbp, r->rsp,
		r->rax, r->rbx, r->rcx, r->rdx,
		r->r8, r->r9, r->r10, r->r11,
		r->r12, r->r13, r->r14, r->r15,
		r->cs, r->ss, r->rflags, r->int_no, r->err_code
	);
}

#define M(e) [e] = #e
const char * signal_names[256] = {
	M(SIGHUP),
	M(SIGINT),
	M(SIGQUIT),
	M(SIGILL),
	M(SIGTRAP),
	M(SIGABRT),
	M(SIGEMT),
	M(SIGFPE),
	M(SIGKILL),
	M(SIGBUS),
	M(SIGSEGV),
	M(SIGSYS),
	M(SIGPIPE),
	M(SIGALRM),
	M(SIGTERM),
	M(SIGUSR1),
	M(SIGUSR2),
	M(SIGCHLD),
	M(SIGPWR),
	M(SIGWINCH),
	M(SIGURG),
	M(SIGPOLL),
	M(SIGSTOP),
	M(SIGTSTP),
	M(SIGCONT),
	M(SIGTTIN),
	M(SIGTTOUT),
	M(SIGVTALRM),
	M(SIGPROF),
	M(SIGXCPU),
	M(SIGXFSZ),
	M(SIGWAITING),
	M(SIGDIAF),
	M(SIGHATE),
	M(SIGWINEVENT),
	M(SIGCAT),
};

static int data_read_bytes(pid_t pid, uintptr_t addr, char * buf, size_t size) {
	for (unsigned int i = 0; i < size; ++i) {
		if (ptrace(PTRACE_PEEKDATA, pid, (void*)addr++, &buf[i])) {
			return 1;
		}
	}
	return 0;
}

static int data_read_int(pid_t pid, uintptr_t addr) {
	int x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(int));
	return x;
}

static uintptr_t data_read_ptr(pid_t pid, uintptr_t addr) {
	uintptr_t x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(uintptr_t));
	return x;
}

static void string_arg(pid_t pid, uintptr_t ptr, size_t maxsize) {
	FILE * logfile = stdout;

	if (ptr == 0) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "\"");

	size_t size = 0;
	uint8_t buf = 0;

	do {
		long result = ptrace(PTRACE_PEEKDATA, pid, (void*)ptr, &buf);
		if (result != 0) break;
		if (!buf) {
			fprintf(logfile, "\"");
			return;
		}

		if (buf == '\\') fprintf(logfile, "\\\\");
		else if (buf == '"') fprintf(logfile, "\\\"");
		else if (buf >= ' ' && buf < '~') fprintf(logfile, "%c", buf);
		else if (buf == '\r') fprintf(logfile, "\\r");
		else if (buf == '\n') fprintf(logfile, "\\n");
		else fprintf(logfile, "\\x%02x", buf);

		ptr++;
		size++;
		if (size > maxsize) break;
	} while (buf);

	fprintf(logfile, "\"...");
}

static char * last_command = NULL;
static void show_commandline(pid_t pid, int status, struct regs * regs) {

	fprintf(stderr, "[Process %d, $rip=%#zx]\n",
		pid, regs->rip);

	while (1) {
		char buf[4096] = {0};
		rline_exit_string = "";
		rline_exp_set_prompts("(dbg) ", "", 6, 0);
		rline_exp_set_syntax("dbg");
		rline_exp_set_tab_complete_func(NULL); /* TODO */
		if (rline(buf, 4096) == 0) goto _exitDebugger;

		char *nl = strstr(buf, "\n");
		if (nl) *nl = '\0';
		if (!strlen(buf)) {
			if (last_command) {
				strcpy(buf, last_command);
			} else {
				continue;
			}
		} else {
			rline_history_insert(strdup(buf));
			rline_scroll = 0;
			if (last_command) free(last_command);
			last_command = strdup(buf);
		}

		/* Tokenize just the first command */
		char * arg = NULL;
		char * sp = strstr(buf, " ");
		if (sp) {
			*sp = '\0';
			arg = sp + 1;
		}

		if (!strcmp(buf, "show")) {
			if (!arg) {
				fprintf(stderr, "Things that can be shown:\n");
				fprintf(stderr, "   regs\n");
				continue;
			}

			if (!strcmp(arg, "regs")) {
				dump_regs(regs);
			} else {
				fprintf(stderr, "Don't know how to show '%s'\n", arg);
			}
		} else if (!strcmp(buf, "continue") || !strcmp(buf,"c")) {
			int signum = WSTOPSIG(status);
			if (signum == SIGINT) signum = 0;
			ptrace(PTRACE_CONT, pid, NULL, (void*)(uintptr_t)signum);
			return;
		} else if (!strcmp(buf, "print") || !strcmp(buf,"p")) {
			char * fmt = arg;
			char * sp = strstr(arg, " ");
			if (!sp) {
				fprintf(stderr, "usage: print fmt addr\n");
				continue;
			}
			*sp = '\0'; sp++;

			uintptr_t addr = strtoul(sp,NULL,0);

			/* Parse any leading numbers */
			int count = 1;

			if (*fmt >= '1' && *fmt <= '9') {
				count = (*fmt - '0');
				fmt++;
				while (*fmt >= '0' && *fmt <= '9') {
					count *= 10;
					count += (*fmt - '0');
					fmt++;
				}
			}

			/* Parse the format */
			for (int i = 0; i < count; ++i) {
				if (!strcmp(fmt, "x")) {
					uint8_t buf[1];
					data_read_bytes(pid, addr, (char*)buf, 1);
					printf("%02x", buf[0]);
					addr += 1;
				} else if (!strcmp(fmt, "i")) {
					printf("%d", data_read_int(pid,addr));
					addr += sizeof(int);
				} else if (!strcmp(fmt, "l")) {
					printf("%ld", (intptr_t)data_read_ptr(pid,addr));
					addr += sizeof(long);
				} else if (!strcmp(fmt, "p")) {
					printf("%#zx", data_read_ptr(pid,addr));
					addr += sizeof(uintptr_t);
				} else if (!strcmp(fmt, "s")) {
					string_arg(pid,addr,count == 1 ? 30 : count);
					break;
				} else {
					printf("print: invalid format string");
					break;
				}
				if (i + 1 < count) {
					printf(" ");
				}
			}
			printf("\n");
		} else {
			fprintf(stderr, "dbg: unrecognized command '%s'\n", buf);
			continue;
		}
	}

_exitDebugger:
	fprintf(stderr, "Exiting.\n");
	exit(0);
}

static int usage(char * argv[]) {
#define T_I "\033[3m"
#define T_O "\033[0m"
	fprintf(stderr, "usage: %s command...\n"
			"  -h         " T_I "Show this help text." T_O "\n",
			argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "o:")) != -1) {
		switch (opt) {
			case 'h':
				return (usage(argv), 0);
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return usage(argv);
	}

	/* TODO find argv[optind] */
	/* TODO load symbols from it, and from its dependencies... with offsets... from ld.so... */

	pid_t p = fork();
	if (!p) {
		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
		execvp(argv[optind], &argv[optind]);
		return 1;
	} else {
		signal(SIGINT, SIG_IGN);

		while (1) {
			int status = 0;
			pid_t res = waitpid(p, &status, WSTOPPED);

			if (res < 0) {
				if (errno == EINTR) continue;
				fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
			} else {
				if (WIFSTOPPED(status)) {
					if (WSTOPSIG(status) == SIGTRAP) {
						/* Don't care about TRAP right now */
						ptrace(PTRACE_CONT, p, NULL, NULL);
					} else {
						printf("Program received signal %s.\n", signal_names[WSTOPSIG(status)]);

						struct regs regs;
						ptrace(PTRACE_GETREGS, res, NULL, &regs);

						show_commandline(res, status, &regs);
					}
				} else if (WIFSIGNALED(status)) {
					fprintf(stderr, "Process %d was killed by %s.\n", res, signal_names[WTERMSIG(status)]);
					return 0;
				} else if (WIFEXITED(status)) {
					fprintf(stderr, "Process %d exited normally.\n", res);
					return 0;
				}
			}
		}
	}

	return 0;
}
