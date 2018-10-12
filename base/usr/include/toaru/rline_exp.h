#pragma once

#include <_cheader.h>
#include <toaru/rline.h>

_Begin_C_Header

extern int rline_experimental(char * buffer, int buf_size);
extern int rline_exp_set_prompts(char * left, char * right, int left_width, int right_width);
extern int rline_exp_set_shell_commands(char ** cmds, int len);
extern int rline_exp_set_tab_complete_func(rline_callback_t func);
extern int rline_exp_set_syntax(char * name);

_End_C_Header
