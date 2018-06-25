#define DEBUG
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
