/**
 * @brief Dummy library to provide rline to Python, but
 *        our Python port is currently on hold.
 *
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * @copyright 2018-2021 K. Lange
 */
#include <string.h>
#include <toaru/rline.h>

void * rline_exp_for_python(void * _stdin, void * _stdout, char * prompt) {

	rline_exp_set_prompts(prompt, "", strlen(prompt), 0);

	char * buf = malloc(1024);
	memset(buf, 0, 1024);

	rline_exp_set_syntax("python");
	rline_exit_string = "";
	rline(buf, 1024);
	rline_history_insert(strdup(buf));
	rline_scroll = 0;

	return buf;
}

char * rline_exit_string;
int rline_history_count;
