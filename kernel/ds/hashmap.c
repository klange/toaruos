#include <system.h>
#include <logging.h>
#include <list.h>
#include <hashmap.h>

static unsigned int hashmap_default_hash(char * key) {
	unsigned int hash = 0;
	int c;
	/* This is the so-called "sdbm" hash. It comes from a piece of
	 * public domain code from a clone of ndbm. */
	while ((c = *key++)) {
		hash = c + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}

hashmap_t * hashmap_create(int size) {
	hashmap_t * map = malloc(sizeof(hashmap_t));

	map->hash_func = hashmap_default_hash;
	map->size = size;
	map->entries = malloc(sizeof(hashmap_entry_t *) * size);
	memset(map->entries, 0x00, sizeof(hashmap_entry_t *) * size);

	return map;
}

void * hashmap_set(hashmap_t * map, char * key, void * value) {
	unsigned int hash = map->hash_func(key) % map->size;

	hashmap_entry_t * x = map->entries[hash];
	if (!x) {
		hashmap_entry_t * e = malloc(sizeof(hashmap_entry_t));
		e->key   = strdup(key);
		e->value = value;
		e->next = NULL;
		map->entries[hash] = e;
		return NULL;
	} else {
		hashmap_entry_t * p = NULL;
		do {
			if (!strcmp(x->key, key)) {
				void * out = x->value;
				x->value = value;
				return out;
			} else {
				p = x;
				x = x->next;
			}
		} while (x);
		hashmap_entry_t * e = malloc(sizeof(hashmap_entry_t));
		e->key   = strdup(key);
		e->value = value;
		e->next = NULL;

		p->next = e;
		return NULL;
	}
}

void * hashmap_get(hashmap_t * map, char * key) {
	unsigned int hash = map->hash_func(key) % map->size;

	hashmap_entry_t * x = map->entries[hash];
	if (!x) {
		return NULL;
	} else {
		do {
			if (!strcmp(x->key, key)) {
				return x->value;
			}
			x = x->next;
		} while (x);
		return NULL;
	}
}

void * hashmap_remove(hashmap_t * map, char * key) {
	unsigned int hash = map->hash_func(key) % map->size;

	hashmap_entry_t * x = map->entries[hash];
	if (!x) {
		return NULL;
	} else {
		if (!strcmp(x->key, key)) {
			void * out = x->value;
			map->entries[hash] = x->next;
			free(x);
			return out;
		} else {
			hashmap_entry_t * p = x;
			x = x->next;
			do {
				if (!strcmp(x->key, key)) {
					void * out = x->value;
					p->next = x->next;
					free(x);
					return out;
				}
				p = x;
				x = x->next;
			} while (x);
		}
		return NULL;
	}
}

int hashmap_has(hashmap_t * map, char * key) {
	unsigned int hash = map->hash_func(key) % map->size;

	hashmap_entry_t * x = map->entries[hash];
	if (!x) {
		return 0;
	} else {
		do {
			if (!strcmp(x->key, key)) {
				return 1;
			}
			x = x->next;
		} while (x);
		return 0;
	}

}

list_t * hashmap_keys(hashmap_t * map) {
	list_t * l = list_create();

	for (unsigned int i = 0; i < map->size; ++i) {
		hashmap_entry_t * x = map->entries[i];
		while (x) {
			list_insert(l, x->key);
			x = x->next;
		}
	}

	return l;
}

list_t * hashmap_values(hashmap_t * map) {
	list_t * l = list_create();

	for (unsigned int i = 0; i < map->size; ++i) {
		hashmap_entry_t * x = map->entries[i];
		while (x) {
			list_insert(l, x->value);
			x = x->next;
		}
	}

	return l;
}

void hashmap_free(hashmap_t * map) {
	for (unsigned int i = 0; i < map->size; ++i) {
		hashmap_entry_t * x = map->entries[i], * p;
		while (x) {
			p = x;
			x = x->next;
			free(p);
		}
	}
	free(map->entries);
}
