#ifndef KERNEL_ARGS_H
#define KERNEL_ARGS_H

struct kernel_arg {
	char * name;
	char * value;
};

list_t * kernel_args_list;

int args_present(char * karg);
char * args_value(char * karg);
void args_parse(char * _arg);

void legacy_parse_args(void);

#endif
