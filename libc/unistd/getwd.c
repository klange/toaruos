#define _POSIX_C_SOURCE 200112L
#include <unistd.h>

char *getwd(char *buf) {
	return getcwd(buf, 256);
}
