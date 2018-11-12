/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>

_Begin_C_Header

typedef struct {
	hashmap_t * sections;
} confreader_t;

extern confreader_t * confreader_load(const char * file);
extern char * confreader_get(confreader_t * ctx, char * section, char * value);
extern char * confreader_getd(confreader_t * ctx, char * section, char * value, char * def);
extern int confreader_int(confreader_t * ctx, char * section, char * value);
extern int confreader_intd(confreader_t * ctx, char * section, char * value, int def);
extern void confreader_free(confreader_t * conf);
extern int confreader_write(confreader_t * config, const char * file);
extern confreader_t * confreader_create_empty(void);

_End_C_Header
