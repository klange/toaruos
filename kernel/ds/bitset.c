/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Dale Weiler
 */
#include "bitset.h"

#define CEIL(NUMBER, BASE) \
	(((NUMBER) + (BASE) - 1) & ~((BASE) - 1))

void bitset_init(bitset_t *set, size_t size) {
	set->size = CEIL(size, 8);
	set->data = malloc(set->size);
}

void bitset_free(bitset_t *set) {
	free(set->data);
}

static void bitset_resize(bitset_t *set, size_t size) {
	if (set->size >= size) {
		return;
	}
	unsigned char *temp = malloc(size);
	memcpy(temp, set->data, set->size);
	free(set->data);
	set->data = temp;
	set->size = size;
}

void bitset_set(bitset_t *set, size_t bit) {
	size_t index = bit >> 3;
	if (set->size <= index) {
		bitset_resize(set, set->size << 1);
	}
	size_t offset = index & 7;
	size_t mask = 1 << offset;
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
	size_t index = bit >> 3;
	size_t offset = index & 7;
	size_t mask = 1 << offset;
	set->data[index] &= ~mask;
}

int bitset_test(bitset_t *set, size_t bit) {
	size_t index = bit >> 3;
	size_t offset = index & 7;
	size_t mask = 1 << offset;
	return mask & set->data[index];
}

