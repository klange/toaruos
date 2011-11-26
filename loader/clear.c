#include <syscall.h>

int main(int argc, char ** argv) {
	syscall_print("\033[H\033[2J");
	return 0;
}
