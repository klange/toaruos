#include <stdint.h>
#include <stddef.h>
#include <syscall.h>

#define _XLOG(_msg) do { \
	char * msg[] = { \
		__FILE__, \
		(char*)__LINE__, \
		(char*)2, \
		_msg, \
	}; \
	syscall_system_function(12, msg); \
} while (0);

