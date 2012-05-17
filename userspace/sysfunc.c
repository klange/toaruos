/* vim:tabstop=4 shiftwidth=4 noexpandtab
 */
#include <syscall.h>
#include <stdlib.h>
DEFN_SYSCALL2(system_function, 43, int, char **);

int main(int argc, char ** argv) {
	if (argc < 2) return 1;
	return syscall_system_function(atoi(argv[1]), &argv[2]);
}
