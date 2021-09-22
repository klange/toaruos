#pragma once

long ptrace_attach(pid_t pid);
long ptrace_self(void);
long ptrace_signal(int signal, int reason);
long ptrace_continue(pid_t pid, int signum);
