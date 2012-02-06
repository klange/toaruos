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

shm_node_t * shm_get_node(char * shm_path, tree_node_t * from) {

	char *pch, *save;
	pch = strtok_r(shm_path, SHM_PATH_SEPARATOR, &save);

	tree_node_t * tnode = from;
	foreach(node, tnode->children) {
		tree_node_t * _node = (tree_node_t *)node->value;
		shm_node_t *  snode = (shm_node_t *)_node->value;

		if (!strcmp(snode->name, pch)) {
			if (*save == '\0') {
				return snode;
			}
			return shm_get_node(save, _node);
		}
	}
	return NULL;
}

shm_node_t * shm_create_node(char * shm_path, tree_node_t * from) {
	char *pch, *save;
	pch = strtok_r(shm_path, SHM_PATH_SEPARATOR, &save);

	tree_node_t * tnode = from;
	foreach(node, tnode->children) {
		tree_node_t * _node = (tree_node_t *)node->value;
		shm_node_t *  snode = (shm_node_t *)_node->value;

		if (!strcmp(snode->name, pch)) {
			if (*save == '\0') {
				return snode;
			}
			return shm_create_node(save, _node);
		}
	}

	kprintf("Did not find node %s [0x%x 0x%x], creating it.\n", pch, pch, save);

	shm_node_t * nsnode = malloc(sizeof(shm_node_t));
	memcpy(nsnode->name, pch, strlen(pch) + 1);

	tree_node_t * nnode = tree_node_insert_child(shm_tree, from, nsnode);

	if (*save == '\0') {
		return nsnode;
	}
	return shm_create_node(save, nnode);
}

char * shm_negotiate(char * shm_path, uintptr_t address, size_t * size) {
	/* Sanity check */
	if (__builtin_expect(shm_tree == NULL, 0)) {
		shm_tree = tree_create();
	}

	kprintf("[debug] shm_negotiate(%s, 0x%x, 0x%x)\n", shm_path, address, size);

	char * path = malloc(strlen(shm_path)+1);
	memcpy(path, shm_path, strlen(shm_path)+1);
	shm_node_t * node = shm_get_node(path, shm_tree->root);

	if (!node) {
		kprintf("Node not found: %s\n", shm_path);

		memcpy(path, shm_path, strlen(shm_path)+1);
		node = shm_create_node(path, shm_tree->root);

		if (!node) {
			kprintf("And could not be created.\n");
		}


		return NULL;
	}

	return NULL;
}

int shm_free(char * shm_path) {
	
	return 0;
}
