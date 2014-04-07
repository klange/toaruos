#ifndef KL_HASHMAP_H
#define KL_HASHMAP_H

#include "list.h"

#ifdef _KERNEL_
#	include <system.h>
#else
#	include <string.h>
#	include <stddef.h>
#	include <stdlib.h>
#endif

typedef unsigned int (*hashmap_hash_t) (void * key);
typedef int (*hashmap_comp_t) (void * a, void * b);
typedef void (*hashmap_free_t) (void *);
typedef void * (*hashmap_dupe_t) (void *);

typedef struct hashmap_entry {
	char * key;
	void * value;
	struct hashmap_entry * next;
} hashmap_entry_t;

typedef struct hashmap {
	hashmap_hash_t hash_func;
	hashmap_comp_t hash_comp;
	hashmap_dupe_t hash_key_dup;
	hashmap_free_t hash_key_free;
	hashmap_free_t hash_val_free;
	size_t         size;
	hashmap_entry_t ** entries;
} hashmap_t;

hashmap_t * hashmap_create(int size);
hashmap_t * hashmap_create_int(int size);
void * hashmap_set(hashmap_t * map, void * key, void * value);
void * hashmap_get(hashmap_t * map, void * key);
void * hashmap_remove(hashmap_t * map, void * key);
int hashmap_has(hashmap_t * map, void * key);
list_t * hashmap_keys(hashmap_t * map);
list_t * hashmap_values(hashmap_t * map);
void hashmap_free(hashmap_t * map);

unsigned int hashmap_string_hash(void * key);
int hashmap_string_comp(void * a, void * b);
void * hashmap_string_dupe(void * key);

#endif
