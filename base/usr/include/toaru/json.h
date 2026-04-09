#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

_Begin_C_Header

enum JSON_Type {
	JSON_TYPE_OBJECT,
	JSON_TYPE_ARRAY,
	JSON_TYPE_STRING,
	JSON_TYPE_NUMBER,
	JSON_TYPE_BOOL,
	JSON_TYPE_NULL
};

struct JSON_Value {
	enum JSON_Type type;
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

extern int json_serialize(FILE * f, struct JSON_Value * thing, int indent);
extern int json_serialize_string(FILE * f, const char * str);
extern int json_serialize_array(FILE * f, list_t * arr, int indent);
extern int json_serialize_object(FILE * f, hashmap_t * obj, int indent);
extern int json_write_file(const char * filename, struct JSON_Value * thing);

extern struct JSON_Value * json_create_number(double val);
extern struct JSON_Value * json_create_string(const char * orig);
extern struct JSON_Value * json_create_bool(int val);
extern struct JSON_Value * json_create_null(void);
extern struct JSON_Value * json_create_empty_object(void);
extern struct JSON_Value * json_create_bool(int val);
extern struct JSON_Value * json_create_null(void);
extern struct JSON_Value * json_object_get(struct JSON_Value * obj, char * key);
extern struct JSON_Value * json_object_set(struct JSON_Value * obj, char * key, struct JSON_Value * value);
extern struct JSON_Value * json_create_empty_array(void);
extern struct JSON_Value * json_array_append(struct JSON_Value * array, struct JSON_Value * val);

_End_C_Header
