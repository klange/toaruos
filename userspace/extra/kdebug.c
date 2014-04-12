#include <syscall.h>

int main(int argc, char * argv[]) {
	int pid = syscall_system_function(7, NULL);
	return syscall_wait(pid);
}
