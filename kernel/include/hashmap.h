#ifndef KL_HASHMAP_H
#define KL_HASHMAP_H

#include <list.h>

typedef unsigned int (*hashmap_hash_t) (char * key);

typedef struct hashmap_entry {
	char * key;
	void * value;
	struct hashmap_entry * next;
} hashmap_entry_t;

typedef struct hashmap {
	hashmap_hash_t hash_func;
	size_t         size;
	hashmap_entry_t ** entries;
} hashmap_t;

hashmap_t * hashmap_create(int size);
void * hashmap_set(hashmap_t * map, char * key, void * value);
void * hashmap_get(hashmap_t * map, char * key);
void * hashmap_remove(hashmap_t * map, char * key);
int hashmap_has(hashmap_t * map, char * key);
list_t * hashmap_keys(hashmap_t * map);
list_t * hashmap_values(hashmap_t * map);
void hashmap_free(hashmap_t * map);

#endif
