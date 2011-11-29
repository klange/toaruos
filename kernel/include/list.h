/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * 
 * General-purpose list implementations.
 */
#include <system.h>

typedef struct node {
	struct node * next;
	struct node * prev;
	void * value;
} node_t;

typedef struct {
	node_t * head;
	node_t * tail;
	size_t length;
} list_t;

void list_destroy(list_t * list);

void list_free(list_t * list);

void list_insert(list_t * list, void * item);

list_t * list_create();

void list_remove(list_t * list, size_t index);

void list_delete(list_t * list, node_t * node);

list_t * list_copy(list_t * original);
