#include <regex.h>
#include <stdio.h>

int regcomp(regex_t *preg, const char *regex, int cflags) {
	fprintf(stderr, "regcomp is a stub!\n");
	return REG_ENOSYS;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags) {
	fprintf(stderr, "regexec is a stub!\n");
	return REG_ENOSYS;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) {
	fprintf(stderr, "regerror is a stub!\n");
	return REG_ENOSYS;
}

void regfree(regex_t *preg) {
	fprintf(stderr, "regfree is a stub!\n");
}
