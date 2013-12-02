#ifndef KERNEL_ARGS_H
#define KERNEL_ARGS_H

int args_present(char * karg);
char * args_value(char * karg);
void args_parse(char * _arg);

void legacy_parse_args(void);

#endif
