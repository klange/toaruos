#pragma once

#include <stddef.h>
#include <kernel/string.h>
#include <kernel/list.h>

typedef unsigned int (*hashmap_hash_t) (const void * key);
typedef int (*hashmap_comp_t) (const void * a, const void * b);
typedef void (*hashmap_free_t) (void *);
typedef void * (*hashmap_dupe_t) (const void *);

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

extern hashmap_t * hashmap_create(int size);
extern hashmap_t * hashmap_create_int(int size);
extern void * hashmap_set(hashmap_t * map, const void * key, void * value);
extern void * hashmap_get(hashmap_t * map, const void * key);
extern void * hashmap_remove(hashmap_t * map, const void * key);
extern int hashmap_has(hashmap_t * map, const void * key);
extern list_t * hashmap_keys(hashmap_t * map);
extern list_t * hashmap_values(hashmap_t * map);
extern void hashmap_free(hashmap_t * map);

extern unsigned int hashmap_string_hash(const void * key);
extern int hashmap_string_comp(const void * a, const void * b);
extern void * hashmap_string_dupe(const void * key);
extern int hashmap_is_empty(hashmap_t * map);

