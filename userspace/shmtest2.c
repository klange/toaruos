/*
 * shmtest2
 * It's a test app. For shm.
 */
#include <sys/types.h>
#include <stdint.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *)

#define KEY "shm_test3.mem"
#define MAGIC 111

int client_proc(uint32_t size) {
	volatile unsigned char * mem = (volatile unsigned char *)syscall_shm_obtain(KEY, &size);

	while (mem[0] != MAGIC) {}
	mem[0] = (uint8_t)(MAGIC + 1);

	for (uint32_t i = 1; i < size; i++) {
		if (mem[i] != (unsigned char)i) {
			printf("Verification at 0x%x (i=%d) failed; expected=%d got=%d\n", &mem[i], i, (uint8_t)i, mem[i]);
			return 1;
		}
	}

	printf("Client: verification passed. Exiting.\n");
	return 0;
}

int server_proc(uint32_t size) {
	volatile unsigned char * mem = (volatile unsigned char *)syscall_shm_obtain(KEY, &size);

	for (uint32_t i = 1; i < size; i++) {
		mem[i] = (unsigned char)i;
	}
	mem[0] = MAGIC;
	printf("Server: Written memory space.\n");

	while (mem[0] == MAGIC) {}
	return 0;
}

int main (int argc, char ** argv) {
	if (argc < 2) {
		printf("usage: %s [size]\n", argv[0]);
	}

	int size = atoi(argv[1]);

	if (!fork()) {
		return server_proc(size);
	} else {
		return client_proc(size);
	}
}
