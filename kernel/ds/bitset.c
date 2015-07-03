/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Dale Weiler
 *               2015 Kevin Lange
 */
#include "bitset.h"

#define CEIL(NUMBER, BASE) \
	(((NUMBER) + (BASE) - 1) & ~((BASE) - 1))

#define iom \
	size_t index  = bit >> 3; \
	bit = bit - index * 8; \
	size_t offset = bit & 7; \
	size_t mask   = 1 << offset;

void bitset_init(bitset_t *set, size_t size) {
	set->size = CEIL(size, 8);
	set->data = calloc(set->size, 1);
}

void bitset_free(bitset_t *set) {
	free(set->data);
}

static void bitset_resize(bitset_t *set, size_t size) {
	if (set->size >= size) {
		return;
	}

	set->data = realloc(set->data, size);
	memset(set->data + set->size, 0, size - set->size);
	set->size = size;
}

void bitset_set(bitset_t *set, size_t bit) {
	iom;
	if (set->size <= index) {
		bitset_resize(set, set->size << 1);
	}
	set->data[index] |= mask;
}

int bitset_ffub(bitset_t *set) {
	for (size_t i = 0; i < set->size * 8; i++) {
		if (bitset_test(set, i)) {
			continue;
		}
		return (int)i;
	}
	return -1;
}

void bitset_clear(bitset_t *set, size_t bit) {
	iom;
	set->data[index] &= ~mask;
}

int bitset_test(bitset_t *set, size_t bit) {
	iom;
	return !!(mask & set->data[index]);
}

