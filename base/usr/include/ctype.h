#pragma once

extern int isalnum(int c);
extern int isalpha(int c);
extern int isdigit(int c);
extern int islower(int c);
extern int isprint(int c);
extern int isgraph(int c);
extern int iscntrl(int c);
extern int isgraph(int c);
extern int ispunct(int c);
extern int isspace(int c);
extern int isupper(int c);
extern int isxdigit(int c);

extern int isascii(int c);

extern int tolower(int c);
extern int toupper(int c);

/* Derived from newlib */
#define _U  01
#define _L  02
#define _N  04
#define _S  010
#define _P  020
#define _C  040
#define _X  0100
#define _B  0200

extern char _ctype_[256];

