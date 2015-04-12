#include <stdio.h>

#include "lib/confreader.h"

int main(int argc, char * argv[]) {
	confreader_t * conf = confreader_load("/etc/test.conf");

	assert(!strcmp(confreader_get(conf, "", "hey"), "yeah"));
	assert(confreader_get(conf, "", "foo") == NULL);

	assert(!strcmp(confreader_get(conf, "sectiona", "foo"), "bar"));
	assert(!strcmp(confreader_get(conf, "sectiona", "baz"), "qix"));
	assert(confreader_get(conf, "sectiona", "herp") == NULL);

	assert(!strcmp(confreader_get(conf, "sectionb", "lol"), "butts"));
	assert(confreader_get(conf, "sectionb", "foo") == NULL);

	confreader_free(conf);

	return 0;
}
