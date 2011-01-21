#ifndef ARRAY_H
#define ARRAY_H
#include <system.h>

typedef void * type_t;
typedef signed char (*lessthan_predicate_t)(type_t, type_t);
typedef struct {
	type_t *array;
	uint32_t size;
	uint32_t max_size;
	lessthan_predicate_t less_than;
} ordered_array_t;

signed char standard_lessthan_predicate(type_t a, type_t b);

ordered_array_t create_ordered_array(uint32_t max_size, lessthanpredicate_t less_than);
ordered_array_t place_ordered_array(void *addr, uint32_t max_size, lessthan_predicate_t less_than);

void destroy_ordered_array(ordered_array_t *array);

void insert_ordered_array(type_t item, ordered_array_t *array);

type_t loopup_ordered_array(uint32_t i, ordered_array_t *array);

void remove_ordered_array(uint32_t i, ordered_array_t *array);


#endif
