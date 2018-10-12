/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>

_Begin_C_Header

typedef struct {
	hashmap_t * sections;
} confreader_t;

confreader_t * confreader_load(const char * file);
char * confreader_get(confreader_t * ctx, char * section, char * value);
char * confreader_getd(confreader_t * ctx, char * section, char * value, char * def);
int confreader_int(confreader_t * ctx, char * section, char * value);
int confreader_intd(confreader_t * ctx, char * section, char * value, int def);
void confreader_free(confreader_t * conf);

_End_C_Header
