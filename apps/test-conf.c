/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * test-conf - simple test app for confreader
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <toaru/confreader.h>


int main(int argc, char * argv[]) {
	confreader_t * conf = confreader_load("/etc/demo.conf");

	fprintf(stderr, "test 1\n");
	assert(confreader_get(conf, "", "test") != NULL);
	assert(!strcmp(confreader_get(conf, "", "test"),"hello"));

	fprintf(stderr, "test 2\n");
	assert(!strcmp(confreader_get(conf,"sec","tion"),"test"));

	return 0;
}
