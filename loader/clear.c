#include <syscall.h>

int main(int argc, char ** argv) {
	syscall_print("\033[J");
	return 0;
}
