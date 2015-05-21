/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef BITSET_H
#define BITSET_H
#include <system.h>

typedef struct {
	unsigned char *data;
	size_t size;
} bitset_t;

void bitset_init(bitset_t *set, size_t size);
void bitset_free(bitset_t *set);
void bitset_set(bitset_t *set, size_t bit);
void bitset_clear(bitset_t *set, size_t bit);
int bitset_test(bitset_t *set, size_t bit);
/* Find first unset bit */
int bitset_ffub(bitset_t *set);

#endif
