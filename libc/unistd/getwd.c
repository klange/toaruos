#include <unistd.h>

char *getwd(char *buf) {
	return getcwd(buf, 256);
}
