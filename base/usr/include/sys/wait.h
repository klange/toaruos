#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2

/* This were taken from newlib, but they remain true */
#define WIFEXITED(w)    (((w) & 0xff) == 0)
#define WIFSIGNALED(w)  (((w) & 0x7f) > 0 && (((w) & 0x7f) < 0x7f))
#define WIFSTOPPED(w)   (((w) & 0xff) == 0x7f)
#define WEXITSTATUS(w)  (((w) >> 8) & 0xff)
#define WTERMSIG(w) ((w) & 0x7f)
#define WSTOPSIG    WEXITSTATUS


extern pid_t wait(int*);
extern pid_t waitpid(pid_t, int *, int);
