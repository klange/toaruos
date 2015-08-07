#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
	struct stat s;
	stat(argv[1], &s);
	chmod(argv[1], s.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
	return 0;
}
