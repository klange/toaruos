#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main(int argc, char * argv[]) {
	int fd = open(".",O_RDONLY|O_DIRECTORY);
	return ioctl(fd, IOCTLSYNC, NULL);
}
