/*
 * shmcrash
 * Like shmcrash2, except it's not #2!
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *)
DEFN_SYSCALL1(shm_release, 36, char *)


int main_server(volatile char *shm);
int main_client(volatile char *shm);



int main (int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	printf("(This should fork and the child process (but not the parent) should segfault)\n");

	size_t size = 27;
	volatile char * shm = (char *)syscall_shm_obtain(argv[1], &size);
	if (shm == NULL) {
		return 1;
	}

	int pid = getpid();
	uint32_t f = fork();
	if (getpid() != pid) {
		// Child: client
		return main_client(shm);
	} else {
		// Parent: server
		return main_server(shm);
	}

	return 0;
}


int main_server(volatile char *shm) {
	char c;
	volatile char *s;

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

int main_client(volatile char *shm) {
	int shmid;
	volatile char *s;

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
