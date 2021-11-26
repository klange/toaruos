/**
 * @file  kernel/misc/tokenize.c
 * @brief Wrapper around strtok_r, used to turn strings into arrays.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2021 K. Lange
 */
#include <kernel/string.h>
#include <kernel/tokenize.h>

int tokenize(char * str, const char * sep, char **buf) {
	char * pch_i;
	char * save_i;
	int    argc = 0;
	pch_i = strtok_r(str,sep,&save_i);
	if (!pch_i) { return 0; }
	while (pch_i != NULL) {
		buf[argc] = (char *)pch_i;
		++argc;
		pch_i = strtok_r(NULL,sep,&save_i);
	}
	buf[argc] = NULL;
	return argc;
}
