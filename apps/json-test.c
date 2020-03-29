#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <toaru/json.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

typedef struct JSON_Value Value;

int main(int argc, char * argv[]) {

	{
		Value * result = json_parse("\"foo bar baz\"");
		assert(result && result->type == JSON_TYPE_STRING);
		assert(strcmp(result->string, "foo bar baz") == 0);
	}

	{
		Value * result = json_parse("\"foo \\nbar baz\"");
		assert(result && result->type == JSON_TYPE_STRING);
		assert(strcmp(result->string, "foo \nbar baz") == 0);
	}

	{
		Value * result = json_parse("-123");
		assert(result && result->type == JSON_TYPE_NUMBER);
		assert(fabs(result->number - (-123.0) < 0.0001));
	}

	{
		Value * result = json_parse("2e3");
		assert(result && result->type == JSON_TYPE_NUMBER);
		assert(fabs(result->number - (2000.0) < 0.0001));
	}

	{
		Value * result = json_parse("0.124");
		assert(result && result->type == JSON_TYPE_NUMBER);
		assert(fabs(result->number - (0.124) < 0.0001));
	}

	{
		Value * result = json_parse("[ 1, 2, 3 ]");
		assert(result && result->type == JSON_TYPE_ARRAY);
		assert(result->array->length == 3);

		assert(fabs(((Value *)list_dequeue(result->array)->value)->number - 1.0) < 0.0001);
		assert(fabs(((Value *)list_dequeue(result->array)->value)->number - 2.0) < 0.0001);
		assert(fabs(((Value *)list_dequeue(result->array)->value)->number - 3.0) < 0.0001);
	}

	{
		Value * result = json_parse("true");
		assert(result && result->type == JSON_TYPE_BOOL);
		assert(result->boolean == 1);
	}

	{
		Value * result = json_parse("false");
		assert(result && result->type == JSON_TYPE_BOOL);
		assert(result->boolean == 0);
	}

	{
		Value * result = json_parse("null");
		assert(result && result->type == JSON_TYPE_NULL);
	}

	{
		Value * result = json_parse("torbs");
		assert(!result);
	}

	{
		Value * result = json_parse("{\"foo\": \"bar\", \"bix\": 123}");
		assert(result && result->type == JSON_TYPE_OBJECT);

		hashmap_t * hash = result->object;
		assert(hashmap_get(hash, "foo"));
		assert(((Value *)hashmap_get(hash, "foo"))->type == JSON_TYPE_STRING);
		assert(strcmp(((Value *)hashmap_get(hash, "foo"))->string, "bar") == 0);
		assert(((Value *)hashmap_get(hash, "bix"))->type == JSON_TYPE_NUMBER);
		assert(fabs(((Value *)hashmap_get(hash, "bix"))->number - 123.0) < 0.00001);
	}

	return 0;
}
