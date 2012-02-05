/*
 * shm-server - client program to demonstrate shared memory.
 */
#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL2(shm_obtain, 35, char *, int)
DEFN_SYSCALL1(shm_release, 36, char *)

#define SHMSZ	27

int main(int argc, char ** argv) {
	char c;
	volatile char *shm;
	volatile char *s;

	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}
	char * key = argv[1];

	/*
	 * Attach to the shared memory chunk
	 */
	if ((shm = (char *)syscall_shm_obtain(key, SHMSZ)) == (char *) NULL) {
		return 1;
	}
	printf("Server: mounted to 0x%x\n", shm);

	/*
	 * Now put some things into the memory for the
	 * other process to read.
	 */
	s = shm;

	for (c = 'a'; c <= 'z'; c++)
		*s++ = c;
	*s = '\0';


	/*
	 * Finally, we wait until the other process 
	 * changes the first character of our memory
	 * to '*', indicating that it has read what 
	 * we put there.
	 */
	while (*shm != '*') {}

	return 0;
}
