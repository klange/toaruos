/*
 * reboot
 *
 * Reboot the system.
 */
#include <stdio.h>
#include <syscall.h>

int main(int argc, char ** argv) {
	if (syscall_reboot() < 0) {
		printf("%s: permission denied\n", argv[0]);
	}
	return 1;
}
