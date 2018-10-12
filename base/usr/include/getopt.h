#pragma once

#include <_cheader.h>

_Begin_C_Header

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern char * optarg;
extern int optind, opterr, optopt;

extern int getopt_long(int argc, char * const argv[], const char *optstring, const struct option *longopts, int *longindex);

#define no_argument 0
#define required_argument 1
#define optional_argument 2 /* Unsupported */

_End_C_Header
