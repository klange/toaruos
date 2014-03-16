#ifndef KERNEL_ARGS_H
#define KERNEL_ARGS_H

int args_present(char * karg);
char * args_value(char * karg);
void args_parse(char * _arg);

void early_stage_args(void);
void late_stage_args(void);

#endif
