#pragma once
#include <_cheader.h>

_Begin_C_Header
extern void vwarn(const char*, __builtin_va_list) __attribute__((format(__printf__,1,0)));
extern void vwarnx(const char*, __builtin_va_list)  __attribute__((format(__printf__,1,0)));
extern void verr(int, const char*, __builtin_va_list) __attribute__((format(__printf__,2,0), noreturn));
extern void verrx(int, const char*, __builtin_va_list) __attribute__((format(__printf__,2,0), noreturn));

extern void warn(const char*, ...) __attribute__((format(__printf__,1,2)));
extern void warnx(const char*, ...) __attribute__((format(__printf__,1,2)));
extern void err(int, const char*, ...) __attribute__((format(__printf__,2,3), noreturn));
extern void errx(int, const char*, ...) __attribute__((format(__printf__,2,3), noreturn));
_End_C_Header
