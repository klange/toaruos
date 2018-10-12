#pragma once

#include <_cheader.h>
#include <stddef.h>

_Begin_C_Header

typedef void * iconv_t;

extern iconv_t iconv_open(const char *tocode, const char *fromcode);
extern int iconv_close(iconv_t cd);
extern size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);

_End_C_Header;
