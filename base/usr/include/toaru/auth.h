/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * Authentication Helpers
 *
 * This library allows multiple login programs (login, sudo, glogin)
 * to share authentication code by providing a single palce to check
 * passwords against /etc/master.passwd and to set typical login vars.
 */

#pragma once

#include <_cheader.h>

_Begin_C_Header

/**
 * toaru_auth_check_pass
 *
 * Returns the uid for the request user on success, -1 on failure.
 */
extern int toaru_auth_check_pass(char * user, char * pass);

/**
 * toaru_auth_set_vars
 *
 * Sets various environment variables (HOME, USER, SHELL, etc.)
 * for the current user.
 */
extern void toaru_auth_set_vars(void);

_End_C_Header
