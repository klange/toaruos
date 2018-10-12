#pragma once

#include <stddef.h>

typedef void * iconv_t;

extern iconv_t iconv_open(const char *tocode, const char *fromcode);
extern int iconv_close(iconv_t cd);
extern size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);

