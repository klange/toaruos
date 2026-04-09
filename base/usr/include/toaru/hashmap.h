#pragma once

#include <_cheader.h>

#ifdef _KERNEL_
#	include <kernel/system.h>
#else
#	include <string.h>
#	include <stddef.h>
#	include <stdlib.h>
#endif

#include <toaru/list.h>

_Begin_C_Header

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

/*
 * Hashmap iterator API.
 *
 * How to use:
 *
 *    hashmap_foreach(iter, my_hashmap) {
 *        my_key_type * key;
 *        my_value_type * value;
 *        hashmap_iter_get(&iter, &key, &value);
 *        do_stuff_with(key,value);
 *    }
 *
 */
struct hashmap_iter {
	hashmap_t * map;
	size_t n;
	hashmap_entry_t * cur;
};

extern struct hashmap_iter hashmap_iter_create(hashmap_t * map);
extern int hashmap_iter_get(struct hashmap_iter * iter, void * keyout, void * valout);
extern void hashmap_iter_next(struct hashmap_iter * iter);
extern int hashmap_iter_valid(struct hashmap_iter * iter);

#define hashmap_foreach(itername, hsh) \
	for (struct hashmap_iter itername = hashmap_iter_create(hsh); hashmap_iter_valid(&itername); hashmap_iter_next(&itername))

_End_C_Header
