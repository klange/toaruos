#ifndef TRACE
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#define TRACE(msg,...) do { \
	struct timeval t; gettimeofday(&t, NULL); \
	fprintf(stderr, "%06d.%06d [" TRACE_APP_NAME "] %s:%05d - " msg "\n", t.tv_sec, t.tv_usec, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)
#endif
