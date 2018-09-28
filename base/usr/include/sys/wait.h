#pragma once

#include <sys/types.h>

#define WNOHANG   0x0001
#define WUNTRACED 0x0002
#define WNOKERN   0x0010

/* This were taken from newlib, but they remain true */
#define WIFEXITED(w)    (((w) & 0xff) == 0)
#define WIFSIGNALED(w)  (((w) & 0x7f) > 0 && (((w) & 0x7f) < 0x7f))
#define WIFSTOPPED(w)   (((w) & 0xff) == 0x7f)
#define WEXITSTATUS(w)  (((w) >> 8) & 0xff)
#define WTERMSIG(w) ((w) & 0x7f)
#define WSTOPSIG    WEXITSTATUS


#ifndef _KERNEL_
extern pid_t wait(int*);
extern pid_t waitpid(pid_t, int *, int);
#endif
