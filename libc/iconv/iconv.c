#include <iconv.h>
#include <errno.h>
#include <string.h>

struct _iconv_state {
	char *tocode;
	char *fromcode;
};

iconv_t iconv_open(const char *tocode, const char *fromcode) {
	errno = EINVAL;
	return (iconv_t)-1;

#if 0
	struct _iconv_state * state = malloc(sizeof(struct _iconv_state));

	state->tocode = strdup(tocode);
	state->fromcode = strdup(fromcode);

	return (iconv_t)state;
#endif
}

int iconv_close(iconv_t cd) {
	struct _iconv_state * state = (struct _iconv_state*)cd;

	free(state->tocode);
	free(state->fromcode);

	free(cd);

	return 0;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) {
	return -1;
}
