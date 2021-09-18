#include <unistd.h>

int getpgrp() {
	return getpgid(0);
}
