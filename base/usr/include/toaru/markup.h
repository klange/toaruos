#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>

_Begin_C_Header

struct markup_tag {
	char * name;
	hashmap_t * options;
};

struct markup_state;
typedef int (*markup_callback_tag_open)(struct markup_state * self, void * user, struct markup_tag * tag);
typedef int (*markup_callback_tag_close)(struct markup_state * self, void * user, char * tag_name);
typedef int (*markup_callback_data)(struct markup_state * self, void * user, char * data);

extern struct markup_state * markup_init(void * user, markup_callback_tag_open open, markup_callback_tag_close close, markup_callback_data data);
extern int markup_free_tag(struct markup_tag * tag);
extern int markup_parse(struct markup_state * state, char c);
extern int markup_finish(struct markup_state * state);

_End_C_Header
