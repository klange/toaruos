/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */

#pragma once

#include <_cheader.h>

_Begin_C_Header

extern int toaru_auth_check_pass(char * user, char * pass);
extern void toaru_auth_set_vars(void);

_End_C_Header
