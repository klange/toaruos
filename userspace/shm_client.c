/*
 * shm-client - client program to demonstrate shared memory.
 */
#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>


DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *)
DEFN_SYSCALL1(shm_release, 36, char *)

#define SHMSZ	27

int main(int argc, char ** argv) {
	int shmid;
	volatile char *shm;
	volatile char *s;

	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}
	char * key = argv[1];

	/*
	 * Attach the segment to our data space.
	 */
	size_t size = SHMSZ;
	malloc(9 * 0x1000); // Make our heap a bit different from the server
	if ((shm = (char *)syscall_shm_obtain(key, &size)) == (char *) NULL) {
		printf("Client: syscall_shm_mount returned NULL!\n");
		return 1;
	}
	printf("Client: mounted to 0x%x\n", shm);

	/*
	 * Now read what the server put in the memory.
	 */
	while (*shm != 'a');
	for (s = shm; *s != '\0'; s++)
		printf("%c", *s);
	printf("\n");

	/*
	 * Finally, change the first character of the 
	 * segment to '*', indicating we have read 
	 * the segment.
	 */
	*shm = '*';

	return 0;
}
