#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

_Begin_C_Header

#define JSON_TYPE_OBJECT 0
#define JSON_TYPE_ARRAY  1
#define JSON_TYPE_STRING 2
#define JSON_TYPE_NUMBER 3
#define JSON_TYPE_BOOL   4
#define JSON_TYPE_NULL   5

struct JSON_Value {
	int type;
	union {
		char * string;
		double number;
		list_t * array;
		hashmap_t * object;
		int boolean;
	};
};

#define JSON_KEY(v,k) ((struct JSON_Value *)(hashmap_get(v->object,k)))
#define JSON_IND(v,i) ((struct JSON_Value *)(list_index(v->array,i)))

/**
 * json_free
 *
 * Free a struct JSON_Value, and its contents recursively if it's an array,
 * object, string, etc.
 */
extern void json_free(struct JSON_Value *);

/**
 * json_parse
 *
 * Parse a string into a JSON_Value
 */
extern struct JSON_Value * json_parse(const char *);

/**
 * json_parse_file
 *
 * Open a file path and parse its contents as JSON
 * (Convenience function)
 */
extern struct JSON_Value * json_parse_file(const char * filename);

_End_C_Header
