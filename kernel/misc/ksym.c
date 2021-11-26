/**
 * @file  kernel/misc/ksym.c
 * @brief Kernel symbol table management.
 *
 * Essentially some wrappers around a hashmap; allows different
 * boot methods to provide symbol tables for use with linking
 * kernel modules.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/types.h>
#include <kernel/hashmap.h>
#include <kernel/assert.h>
#include <kernel/ksym.h>

static hashmap_t * ksym_hash = NULL;

void ksym_install(void) {
	assert(ksym_hash == NULL);
	ksym_hash = hashmap_create(20);
}

void ksym_bind(const char * symname, void * value) {
	assert(ksym_hash != NULL);

	hashmap_set(ksym_hash, symname, value);
}

void * ksym_lookup(const char * symname) {
	return hashmap_get(ksym_hash, symname);
}

list_t * ksym_list(void) {
	assert(ksym_hash != NULL);
	return hashmap_keys(ksym_hash);
}

hashmap_t * ksym_get_map(void) {
	return ksym_hash;
}
