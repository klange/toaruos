#pragma once

#include <_cheader.h>
#include <stddef.h>

_Begin_C_Header

typedef ptrdiff_t regoff_t;

typedef struct re_pattern_buffer {
	size_t re_nsub;
	void *__impl;
} regex_t;

typedef struct {
	regoff_t rm_so;
	regoff_t rm_eo;
} regmatch_t;

// Flags for regcomp()
#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NEWLINE 4
#define REG_NOSUB 8

// Flags for regexec()
#define REG_NOTBOL 1
#define REG_NOTEOL 2

// Errors for regcomp() and regexec()
#define REG_OK 0
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ECOLLATE 3
#define REG_ECTYPE 4
#define REG_EESCAPE 5
#define REG_ESUBREG 6
#define REG_EBRACK 7
#define REG_EPAREN 8
#define REG_EBRACE 9
#define REG_BADBR 10
#define REG_ERANGE 11
#define REG_ESPACE 12
#define REG_BADRPT 13

#define REG_ENOSYS -1

extern int regcomp(regex_t *, const char *, int);
extern int regexec(const regex_t *, const char *, size_t, regmatch_t *, int);
extern size_t regerror(int, const regex_t *, char *, size_t);
extern void regfree(regex_t *);

_End_C_Header
