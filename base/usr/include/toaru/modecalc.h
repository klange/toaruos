/**
 * @brief Decode mode strings used in various utilities.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <sys/stat.h>

static mode_t mode_calc(const char * c, mode_t original, mode_t mask, int flags) {
	if (*c >= '0' && *c <= '7') {
		mode_t out = 0;
		while (*c >= '0' && *c <= '7') {
			out <<= 3;
			out |= *c - '0';
			c++;
		}

		if (*c) return (mode_t)-1;
		return out;
	}

	enum ModeAction { MODE_ADD, MODE_DEL, MODE_SET };

	mode_t base = original;
	int capital_X_is_meaningful = !!(flags & 2) || !!(original & (S_IXUSR | S_IXGRP | S_IXOTH));

	while (*c) {
		mode_t current = base;
		mode_t masked = 0;
		int who = 0;

_who:
		switch (*c) {
			case 'u': who |= S_IXUSR; c++; goto _who;
			case 'g': who |= S_IXGRP; c++; goto _who;
			case 'o': who |= S_IXOTH; c++; goto _who;
			case 'a': who |= S_IXUSR | S_IXGRP | S_IXOTH; c++; goto _who;
		}

		if (who == 0) {
			who = S_IXUSR | S_IXGRP | S_IXOTH;
			masked = mask;
		}

_action: (void)0;
		enum ModeAction action = MODE_SET;
		mode_t affect = 0;
		int setuid = 0;
		int setgid = 0;
		int more_actions = 0;

		switch (*c) {
			case '+': action = MODE_ADD; c++; break;
			case '-': action = MODE_DEL; c++; break;
			case '=': action = MODE_SET; c++; break;
			default: goto _invalid;
		}

		switch (*c) {
			case 'u': affect = (current & S_IRWXU) >> 6; c++; goto _apply;
			case 'g': affect = (current & S_IRWXG) >> 3; c++; goto _apply;
			case 'o': affect = (current & S_IRWXO) >> 0; c++; goto _apply;
		}

_permlist:
		switch (*c) {
			case 'r': affect |= S_IROTH; c++; goto _permlist;
			case 'w': affect |= S_IWOTH; c++; goto _permlist;
			case 'x': affect |= S_IXOTH; c++; goto _permlist;
			case 'X': if (capital_X_is_meaningful) { affect |= S_IXOTH; } c++; goto _permlist;
			case 's':
				setuid = (who & S_IXUSR) ? 1 : 0;
				setgid = (who & S_IXGRP) ? 1 : 0;
				c++;
				goto _permlist;
			case '+':
			case '-':
			case '=': more_actions = 1; break;
			case ',': c++; break;
			case '\0': break;
			default: goto _invalid;
		}

_apply: (void)0;
		mode_t applied = (affect * who) & ~masked;
		if (setuid) applied |= S_ISUID;
		if (setgid) applied |= S_ISGID;

		switch (action) {
			case MODE_SET:
				current &= ~(who * S_IRWXO);
				/* fallthrough */
			case MODE_ADD:
				current |= applied;
				break;
			case MODE_DEL:
				current &= ~(applied);
				break;
		}

		base = current;
		if (more_actions) goto _action;
	}

	if (flags & 1) return 0777 & (~base);
	return base;

_invalid:
	return (mode_t)-1;
}

