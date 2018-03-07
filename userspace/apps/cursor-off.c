#include <syscall.h>

int main(int argc, char * argv[]) {
	int x[] = {0xFF,0xFF};
	return syscall_system_function(13, (char **)x);
}
