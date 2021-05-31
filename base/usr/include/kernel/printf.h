#pragma once

#include <kernel/types.h>

__attribute__((format(__printf__,1,2)))
extern int printf(const char *fmt, ...);
extern size_t (*printf_output)(size_t, uint8_t *);
__attribute__((format(__printf__,3,4)))
extern int snprintf(char * str, size_t size, const char * format, ...);
