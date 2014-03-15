#include <fs.h>

#ifndef KERNEL_MOD_SHELL_H
#define KERNEL_MOD_SHELL_H
/*
 * We're going to have a list of shell commands.
 * We'll search through it linearly because I don't
 * care to write a hashmap right now. Maybe later.
 */
struct shell_command {
	char * name;
	int (*function) (fs_node_t * tty, int argc, char * argv[]);
	char * description;
};

extern void debug_shell_install(struct shell_command * sh);
extern void fs_printf(fs_node_t * device, char *fmt, ...);

#define DEFINE_SHELL_FUNCTION(n, desc) \
	static int shell_ ## n (fs_node_t * tty, int argc, char * argv[]); \
	static struct shell_command shell_ ## n ## _desc = { \
		.name = #n , \
		.function = &shell_ ## n , \
		.description = desc \
	}; \
	static int shell_ ## n (fs_node_t * tty, int argc, char * argv[])

#define BIND_SHELL_FUNCTION(name) \
	debug_shell_install(&shell_ ## name ## _desc);

#endif
