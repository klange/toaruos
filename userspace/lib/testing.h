#ifndef _TESTING_H
#define _TESTING_H

#include <stdarg.h>

#define INFO(...)  notice("INFO",  __VA_ARGS__)
#define WARN(...)  notice("WARN",  __VA_ARGS__)
#define DONE(...)  notice("DONE",  __VA_ARGS__)
#define PASS(...)  notice("PASS",  __VA_ARGS__)
#define FAIL(...)  notice("FAIL",  __VA_ARGS__)
#define FATAL(...) notice("FATAL", __VA_ARGS__)

void notice(char * type, char * fmt, ...);

#endif
