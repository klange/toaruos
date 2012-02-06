/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef SHM_H
#define SHM_H

#include <types.h>

#define SHM_PATH_SEPARATOR "."

typedef struct {
	char name[256];
} shm_node_t;

char * shm_negotiate(char * shm_path, uintptr_t address, size_t * size);
int shm_free(char * shm_path);


#endif
