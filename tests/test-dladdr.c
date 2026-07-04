/*
 * @auto-dep export-dynamic
 */
#include <dlfcn.h>
#include <stdio.h>

/* Arbitrarily chosen */
#include <toaru/list.h>

void try(void * addr, const char * desc) {
	Dl_info_t info;

	fprintf(stderr, "%s: dladdr(%p, ...)\n", desc, addr);

	if (!dladdr(addr, &info)) {
		fprintf(stderr, "test-dladdr: dladdr failed for %s\n", desc);
	} else {
		fprintf(stderr, "%s got:\n"
			"   dli_fname = %s\n"
			"   dli_fbase = %p\n"
			"   dli_sname = %s\n"
			"   dli_saddr = %p\n",
			desc,
			info.dli_fname,
			info.dli_fbase,
			info.dli_sname,
			info.dli_saddr);
	}

}

int main(int argc, char * argv[]) {
	try(dlsym(RTLD_NEXT, "list_insert"), "list_insert");
	try((char*)dlsym(RTLD_NEXT, "list_insert") + 40, "list_insert+40");
	try((char*)dlsym(RTLD_NEXT, "list_insert") + 400, "list_insert+400");
	try(&main, "&main");

	dlopen("/lib/libtoaru_graphics.so", RTLD_NOW);

	try(dlsym(RTLD_NEXT, "flip"), "flip");
	try((char*)dlsym(RTLD_NEXT, "flip")+400, "flip+400");

	try((void*)(uintptr_t)0x50000000, "0x50000000");

	return 0;
}
