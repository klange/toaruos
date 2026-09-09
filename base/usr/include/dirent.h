#pragma once

#include <_cheader.h>
#include <bits/dirent.h>

_Begin_C_Header

int alphasort(const struct dirent ** c1, const struct dirent ** c2);
int scandir(const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **));

_End_C_Header
