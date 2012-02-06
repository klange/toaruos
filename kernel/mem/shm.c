/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Shared Memory Negotiation
 *
 */
#include <system.h>
#include <syscall.h>
#include <process.h>
#include <logging.h>
#include <fs.h>
#include <pipe.h>
#include <shm.h>
#include <tree.h>
#include <list.h>

tree_t * shm_tree = NULL;

void shm_install() {
	LOG(INFO, "Installing SHM");
}

char * shm_negotiate(char * shm_path, uintptr_t address, size_t * size) {
	/* Sanity check */
	if (__builtin_expect(shm_tree == NULL, 0)) {
		shm_tree = tree_create();
	}

	return 0;
}

int shm_free(char * shm_path) {
	
	return 0;
}
