#pragma once
#include <_cheader.h>
#include <stdio.h>

_Begin_C_Header

extern size_t __fbufsize(FILE * stream);
extern void __fpurge(FILE * stream);
extern size_t __fpending(FILE * stream);

_End_C_Header
