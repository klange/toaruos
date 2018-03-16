#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2

extern pid_t wait(int*);
extern pid_t waitpid(pid_t, int *, int);
