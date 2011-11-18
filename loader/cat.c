#include <syscall.h>

int main(int argc, char ** argv) {
	if (argc < 2) {
		syscall_print("Expected a filename.\n");
		return -1;
	} else {
		int i = syscall_open(argv[1], 0, 0);
		if (i < 0) {
			syscall_print("cat: could not open '");
			syscall_print(argv[1]);
			syscall_print("': no such file or directory\n");
			return 1;
		}
		char dat[1024];
		int read = 0;
		while ((read = syscall_read(i, dat, 1024))) {
			for (int j = 0; j < read; ++j) {
				char datum[2] = {dat[j], '\0'};
				syscall_print(datum);
			}
		}
		syscall_close(i);
	}
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
