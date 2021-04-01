#include <string.h>
#include <toaru/rline.h>
/* dummy lib */

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
