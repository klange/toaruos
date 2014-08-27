/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */

#ifndef _TOARU_AUTH_H
#define _TOARU_AUTH_H

int toaru_auth_check_pass(char * user, char * pass);
void toaru_auth_set_vars(void);

#endif /* _TOARU_AUTH_H */
