#include <stdio.h>
#include <stdarg.h>
#include <syscall.h>

#define INFO(...)  notice("INFO",  __VA_ARGS__)
#define WARN(...)  notice("WARN",  __VA_ARGS__)
#define DONE(...)  notice("DONE",  __VA_ARGS__)
#define PASS(...)  notice("PASS",  __VA_ARGS__)
#define FAIL(...)  notice("FAIL",  __VA_ARGS__)
#define FATAL(...) notice("FATAL", __VA_ARGS__)

void notice(char * type, char * fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	/* core-tests header */
	syscall_print("core-tests : ");
	syscall_print(type);
	syscall_print(" : ");
	/* end core-tests header */
	char buffer[1024];
	vsnprintf(buffer, 1024, fmt, argp);
	syscall_print(buffer);
	syscall_print("\n");
}

int main(int argc, char * argv[]) {
	INFO("Hello world!");

	DONE("Finished tests!");
}
