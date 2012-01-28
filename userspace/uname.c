/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * uname
 */
#include <stdio.h>
#include <syscall.h>

DEFN_SYSCALL1(kernel_string_XXX, 25, char *);

int main(int argc, char * argv[]) {
	char _uname[1024];
	syscall_kernel_string_XXX(_uname);

	fprintf(stdout, "%s\n", _uname);
}
