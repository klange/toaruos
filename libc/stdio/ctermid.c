#include <stdio.h>
#include <unistd.h>

static char __ctermid[L_ctermid];

char *ctermid(char *s) {
	if (!s) s = __ctermid;
	ssize_t link_size = readlink("/dev/tty", s, L_ctermid-1);
	if (link_size < 0) return NULL;
	s[link_size] = '\0';
	return s;
}
