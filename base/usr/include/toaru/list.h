#pragma once

#include <_cheader.h>

#ifdef _KERNEL_
#	include <kernel/types.h>
#else
#	include <stdint.h>
#	include <stddef.h>
#	include <assert.h>
#endif

_Begin_C_Header

typedef struct node {
	struct node * next;
	struct node * prev;
	void * value;
	void * owner;
} __attribute__((packed)) node_t;

typedef struct {
	node_t * head;
	node_t * tail;
	size_t length;
} __attribute__((packed)) list_t;

extern void list_destroy(list_t * list);
extern void list_free(list_t * list);
extern void list_append(list_t * list, node_t * item);
extern node_t * list_insert(list_t * list, void * item);
extern list_t * list_create(void);
extern node_t * list_find(list_t * list, void * value);
extern int list_index_of(list_t * list, void * value);
extern void list_remove(list_t * list, size_t index);
extern void list_delete(list_t * list, node_t * node);
extern node_t * list_pop(list_t * list);
extern node_t * list_dequeue(list_t * list);
extern list_t * list_copy(list_t * original);
extern void list_merge(list_t * target, list_t * source);
extern void * list_index(list_t * list, int index);

extern void list_append_after(list_t * list, node_t * before, node_t * node);
extern node_t * list_insert_after(list_t * list, node_t * before, void * item);

extern void list_append_before(list_t * list, node_t * after, node_t * node);
extern node_t * list_insert_before(list_t * list, node_t * after, void * item);

/* Known to conflict with some popular third-party libraries. */
#ifndef TOARU_LIST_NO_FOREACH
#  define foreach(i, list) for (node_t * i = (list)->head; i != NULL; i = i->next)
#  define foreachr(i, list) for (node_t * i = (list)->tail; i != NULL; i = i->prev)
#endif

_End_C_Header
