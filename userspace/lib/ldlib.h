#ifndef LDLIB_H
#define LDLIB_H

#include <syscall.h>
#include <stdint.h>
#include <unistd.h>

DECL_SYSCALL2(shm_obtain, char *, size_t *);

#endif
