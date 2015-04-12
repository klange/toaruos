/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef _CONFREADER_H
#define _CONFREADER_H

#include "hashmap.h"

typedef struct {
	hashmap_t * sections;
} confreader_t;

confreader_t * confreader_load(const char * file);
char * confreader_get(confreader_t * ctx, char * section, char * value);
char * confreader_getd(confreader_t * ctx, char * section, char * value, char * def);
int confreader_int(confreader_t * ctx, char * section, char * value);
int confreader_intd(confreader_t * ctx, char * section, char * value, int def);
void confreader_free(confreader_t * conf);

#endif /* _CONFREADER_H */

