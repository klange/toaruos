/**
 * @brief JSON parser.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#include <toaru/hashmap.h>
#include <toaru/list.h>
#include <toaru/json.h>

typedef struct JSON_Value Value;

const char * json_lib_error = NULL;

/* Internal usage */
struct JSON_Context {
	const char * string;
	int c;
	const char * error;
};

void json_free(Value * v) {
	if (v->type == JSON_TYPE_STRING) {
		free(v->string);
	}
	if (v->type == JSON_TYPE_OBJECT) {
		hashmap_free(v->object);
		free(v->object);
	}
	if (v->type == JSON_TYPE_ARRAY) {
		foreach(node, v->array) {
			json_free(node->value);
		}
		list_free(v->array);
		free(v->array);
	}
	free(v);
}

static Value * value(struct JSON_Context * ctx);

static int peek(struct JSON_Context * ctx) {
	return ctx->string[ctx->c];
}

static void advance(struct JSON_Context * ctx) {
	ctx->c++;
}

static void whitespace(struct JSON_Context * ctx) {
	while (1) {
		int ch = peek(ctx);
		if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') {
			advance(ctx);
		} else {
			break;
		}
	}
}

static Value * string(struct JSON_Context * ctx) {
	if (peek(ctx) != '"') return NULL;
	advance(ctx);

	int size = 4;
	char * tmp = malloc(4);
	tmp[0] = 0;
	int used = 0;

#define add(c) do { \
	if (used + 1 == size) { \
		size *= 2; \
		tmp = realloc(tmp, size); \
	} \
	tmp[used] = c; \
	tmp[used+1] = 0; \
	used++; \
} while (0)

	while (1) {
		int ch = peek(ctx);
		if (ch == 0) goto string_error;
		if (ch == '"') {
			break;
		} else if (ch == '\\') {
			advance(ctx);
			ch = peek(ctx);
			if (ch == '"') add('"');
			else if (ch == '\\') add('\\');
			else if (ch == '/') add('/');
			else if (ch == 'b') add('\b');
			else if (ch == 'f') add('\f');
			else if (ch == 'n') add('\n');
			else if (ch == 'r') add('\r');
			else if (ch == 't') add('\t');
			else if (ch == 'u') {
				/* Parse hex */
				advance(ctx);
				char hex_digits[5];
				if (!isxdigit(peek(ctx))) goto string_error;
				hex_digits[0] = peek(ctx); advance(ctx);
				if (!isxdigit(peek(ctx))) goto string_error;
				hex_digits[1] = peek(ctx); advance(ctx);
				if (!isxdigit(peek(ctx))) goto string_error;
				hex_digits[2] = peek(ctx); advance(ctx);
				if (!isxdigit(peek(ctx))) goto string_error;
				hex_digits[3] = peek(ctx); /* will be advanced later */
				hex_digits[4] = 0;

				uint32_t val = strtoul(hex_digits, NULL, 16);
				if (val < 0x0080) {
					add(val);
				} else if (val < 0x0800) {
					add(0xC0 | (val >> 6));
					add(0x80 | (val & 0x3F));
				} else {
					add(0xE0 | (val >> 12));
					add(0x80 | ((val >> 6) & 0x3F));
					add(0x80 | (val & 0x3F));
				}
			} else {
				goto string_error;
			}
			advance(ctx);
		} else {
			add(ch);
			advance(ctx);
		}
	}

	if (peek(ctx) != '"') {
		ctx->error = "Unexpected EOF?";
		goto string_error;
	}
	advance(ctx);

	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_STRING;
	out->string = strdup(tmp);
	free(tmp);

	return out;

string_error:
	free(tmp);
	return NULL;
}

static Value * object(struct JSON_Context * ctx) {
	if (peek(ctx) != '{') {
		ctx->error = "Expected { (internal error)";
		return NULL;
	}
	advance(ctx);

	Value * out;
	hashmap_t * output = hashmap_create(10);
	output->hash_val_free = (void (*)(void *))json_free;
	whitespace(ctx);

	if (peek(ctx) == '}') {
		advance(ctx);
		goto _object_done;
	}

	while (1) {
		whitespace(ctx);
		Value * s = string(ctx);

		if (!s) {
			ctx->error = "Expected string";
			break;
		}

		whitespace(ctx);

		if (peek(ctx) != ':') {
			ctx->error = "Expected :";
			break;
		}
		advance(ctx);

		Value * v = value(ctx);

		hashmap_set(output, s->string, v);
		json_free(s);

		if (peek(ctx) == '}') {
			advance(ctx);
			goto _object_done;
		}

		if (peek(ctx) != ',') {
			ctx->error = "Expected , or {";
			break;
		}

		advance(ctx);
	}

	hashmap_free(output);
	return NULL;

_object_done:
	out = malloc(sizeof(Value));
	out->type = JSON_TYPE_OBJECT;
	out->object = output;
	return out;
}

static Value * boolean(struct JSON_Context * ctx) {
	int value = -1;
	if (peek(ctx) == 't') {
		advance(ctx);
		if (peek(ctx) != 'r') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		if (peek(ctx) != 'u') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		if (peek(ctx) != 'e') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		value = 1;
	} else if (peek(ctx) == 'f') {
		advance(ctx);
		if (peek(ctx) != 'a') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		if (peek(ctx) != 'l') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		if (peek(ctx) != 's') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		if (peek(ctx) != 'e') { ctx->error = "Invalid literal while parsing bool"; return NULL; }
		advance(ctx);
		value = 0;
	} else { ctx->error = "Invalid literal while parsing bool"; return NULL; }

	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_BOOL;
	out->boolean = value;
	return out;
}

static Value * null(struct JSON_Context * ctx) {
	if (peek(ctx) != 'n') { ctx->error = "Invalid literal while parsing null"; return NULL; }
	advance(ctx);
	if (peek(ctx) != 'u') { ctx->error = "Invalid literal while parsing null"; return NULL; }
	advance(ctx);
	if (peek(ctx) != 'l') { ctx->error = "Invalid literal while parsing null"; return NULL; }
	advance(ctx);
	if (peek(ctx) != 'l') { ctx->error = "Invalid literal while parsing null"; return NULL; }
	advance(ctx);

	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_NULL;
	return out;
}

static Value * number(struct JSON_Context * ctx) {

	double value = 0;
	int sign = 1;
	if (peek(ctx) == '-') {
		/* Negative */
		sign = -1;
		advance(ctx);
	}

	if (peek(ctx) == '0') {
		advance(ctx);
	} else if (isdigit(peek(ctx))) {
		/* Read any digit */
		value = peek(ctx) - '0';
		advance(ctx);
		while (isdigit(peek(ctx))) {
			value *= 10;
			value += peek(ctx) - '0';
			advance(ctx);
		}
	} else {
		ctx->error = "Expected digit";
		return NULL;
	}

	if (peek(ctx) == '.') {
		/* Read fractional part */
		advance(ctx);

		double multiplier = 0.1;
		/* read at least one digit */
		if (!isdigit(peek(ctx))) {
			ctx->error = "Expected digit";
			return NULL;
		}
		while (isdigit(peek(ctx))) {
			value += multiplier * (peek(ctx) - '0');
			multiplier *= 0.1;
			advance(ctx);
		}
	}

	if (peek(ctx) == 'e' || peek(ctx) == 'E') {
		/* Read exponent */
		int exp_sign = 1;
		advance(ctx);
		if (peek(ctx) == '+') advance(ctx);
		else if (peek(ctx) == '-') {
			exp_sign = -1;
			advance(ctx);
		}

		/* read digits */
		if (!isdigit(peek(ctx))) {
			ctx->error = "Expected digit";
			return NULL;
		}
		double exp = peek(ctx) - '0';
		advance(ctx);
		while (isdigit(peek(ctx))) {
			exp *= 10;
			exp += peek(ctx) - '0';
			advance(ctx);
		}

		value = value * pow(10.0,exp * exp_sign);
	}

	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_NUMBER;
	out->number = value * sign;

	return out;
}

static Value * array(struct JSON_Context * ctx) {
	if (peek(ctx) != '[') return NULL;
	advance(ctx);

	whitespace(ctx);

	list_t * output = list_create();
	Value * out;

	if (peek(ctx) == ']') {
		advance(ctx);
		goto _array_done;
	}

	while (1) {
		Value * next = value(ctx);

		if (!next) break;

		list_insert(output, next);

		if (peek(ctx) == ']') {
			advance(ctx);
			goto _array_done;
		}
		if (peek(ctx) != ',') {
			ctx->error = "Expected ,";
			break;
		}

		advance(ctx);
	}

	/* uh oh */
	foreach(node, output) {
		json_free(node->value);
	}
	list_free(output);
	free(output);
	return NULL;

_array_done:
	out = malloc(sizeof(Value));
	out->type = JSON_TYPE_ARRAY;
	out->array = output;
	return out;
}

#define WHITE(c) { \
	Value * out = c; \
	whitespace(ctx); \
	return out; \
}

static Value * value(struct JSON_Context * ctx) {
	whitespace(ctx);
	if (peek(ctx) == '"') WHITE(string(ctx))
	else if (peek(ctx) == '{') WHITE(object(ctx))
	else if (peek(ctx) == '[') WHITE(array(ctx))
	else if (peek(ctx) == '-' || isdigit(peek(ctx))) WHITE(number(ctx))
	else if (peek(ctx) == 't') WHITE(boolean(ctx))
	else if (peek(ctx) == 'f') WHITE(boolean(ctx))
	else if (peek(ctx) == 'n') WHITE(null(ctx))
	ctx->error = "Unexpected value";
	return NULL;
}

Value * json_parse(const char * str) {
	struct JSON_Context ctx;
	ctx.string = str;
	ctx.c = 0;
	ctx.error = NULL;
	json_lib_error = NULL; /* No error yet */
	Value * out = value(&ctx);
	if (!out) json_lib_error = ctx.error; /* Pointer to static string, so this is fine. */
#if 0
	if (!out) {
		fprintf(stderr, "JSON parse error at %d (%c)\n", ctx.c, ctx.string[ctx.c]);
		fprintf(stderr, "%s\n", ctx.error);
		fprintf(stderr, "%s\n", ctx.string);
		for (int i = 0; i < ctx.c; ++i) { fprintf(stderr, " "); }
		fprintf(stderr, "^\n");
	}
#endif
	return out;
}

Value * json_parse_file(const char * filename) {
	FILE * f = fopen(filename, "r");

	if (!f) {
		json_lib_error = strerror(errno);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char * tmp = malloc(size + 1);
	fread(tmp, size, 1, f);
	tmp[size] = 0;

	fclose(f);

	Value * out = json_parse(tmp);
	free(tmp);
	return out;
}

int json_serialize(FILE * f, Value * thing, int indent);

int json_serialize_string(FILE * f, const char * str) {
	fputc('"', f);

	while (*str) {
		switch (*str) {
			//case '/': /* This is valid in the other direction, but serves little purpose, so we don't do it. */
			case '"':
			case '\\':
				fputc('\\', f);
				fputc(*str, f);
				break;

			case '\r': fprintf(f, "\\r"); break; 
			case '\n': fprintf(f, "\\n"); break;
			case '\b': fprintf(f, "\\b"); break;
			case '\f': fprintf(f, "\\f"); break;
			case '\t': fprintf(f, "\\t"); break;

			/* TODO unicode as \u sequences, but whatever, json
			 * can happily accept utf-8 */

			default:
				fputc(*str, f);
				break;
		}
		str++;
	}

	fputc('"', f);
	return 0;
}

int json_serialize_array(FILE * f, list_t * arr, int indent) {
	fprintf(f, "[");
	size_t c = 0;

	if (arr->length < 2) {
		foreach(node, arr) {
			int ret = json_serialize(f, node->value, indent);
			if (ret) return ret;
			if (c + 1 < arr->length) fprintf(f, ", ");
			c++;
		}

		fprintf(f, "]");
		return 0;
	}

	fprintf(f, "\n");
	foreach(node, arr) {
		for (int i = 0; i < indent + 1; ++i) fprintf(f, "  ");
		int ret = json_serialize(f, node->value, indent + 1);
		if (ret) return ret;
		if (c + 1 < arr->length) fprintf(f, ",\n");
		c++;
	}
	for (int i = 0; i < indent; ++i) fprintf(f, "  ");
	fprintf(f, "]");

	return 0;
}

int json_serialize_object(FILE * f, hashmap_t * obj, int indent) {
	int ret = 0;
	int printed = 0;
	fprintf(f, "{\n");

	list_t * keys = hashmap_keys(obj);
	foreach (node, keys) {
		if (printed) fprintf(f, ",\n");
		for (int i = 0; i < indent + 1; ++i) fprintf(f, "  ");
		ret = json_serialize_string(f, (char*)node->value);
		if (ret) goto _bail;
		fprintf(f, ": ");

		Value * val = hashmap_get(obj, node->value);
		ret = json_serialize(f, val, indent + 1);
		printed = 1;
	}

	if (printed) fprintf(f, "\n");

	list_free(keys);
	free(keys);

	for (int i = 0; i < indent; ++i) fprintf(f, "  ");
	fprintf(f, "}");
	return 0;

_bail:
	list_free(keys);
	free(keys);
	return ret;
}

int json_serialize(FILE * f, Value * thing, int indent) {
	switch (thing->type) {
		case JSON_TYPE_NUMBER:
			/* Hope your double printing is good enough */
			fprintf(f, "%f", thing->number);
			return 0;

		case JSON_TYPE_STRING:
			return json_serialize_string(f, thing->string);

		case JSON_TYPE_BOOL:
			fprintf(f, "%s", thing->boolean ? "true" : "false");
			return 0;

		case JSON_TYPE_NULL:
			fprintf(f, "null");
			return 0;

		case JSON_TYPE_ARRAY:
			return json_serialize_array(f, thing->array, indent);

		case JSON_TYPE_OBJECT:
			return json_serialize_object(f, thing->object, indent);

		default:
			json_lib_error = "what";
			return 1;
	}
}

int json_write_file(const char * filename, Value * thing) {
	FILE * f = fopen(filename, "w");
	if (!f) {
		json_lib_error = strerror(errno);
		return 1;
	}

	int ret = json_serialize(f, thing, 0);

	fputc('\n', f);

	fclose(f);
	return ret;
}

/* For external use */
Value * json_create_number(double val) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_NUMBER;
	out->number = val;
	return out;
}

Value * json_create_string(const char * orig) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_STRING;
	out->string = strdup(orig);
	return out;
}

Value * json_take_string(char *str) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_STRING;
	out->string = str;
	return out;
}

Value * json_create_bool(int val) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_BOOL;
	out->boolean = val;
	return out;
}

Value * json_create_null(void) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_NULL;
	return out;
}

Value * json_create_empty_object(void) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_OBJECT;
	out->object = hashmap_create(10);
	out->object->hash_val_free = (void (*)(void *))json_free;
	return out;
}

/* Casting convenience */
Value * json_object_get(Value * obj, char * key) {
	return hashmap_get(obj->object, key);
}

Value * json_object_set(Value * obj, char * key, Value * value) {
	hashmap_set(obj->object, key, value);
	return value;
}

Value * json_create_empty_array(void) {
	Value * out = malloc(sizeof(Value));
	out->type = JSON_TYPE_ARRAY;
	out->array = list_create();
	return out;
}

Value * json_array_append(Value * array, Value * val) {
	list_insert(array->array, val);
	return val;
}
