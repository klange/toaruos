#include <unistd.h>
#include <getopt.h>

char * optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char * const argv[], const char * optstring) {
	return getopt_long(argc, argv, optstring, NULL, 0);
}
