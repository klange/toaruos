#include <syscall.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	int pid = syscall_system_function(7, NULL);
	int status;
	wait(&status);
	return status;
}
