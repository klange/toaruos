/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <ncurses.h>

int main() {
	initscr();
	printw("Hello World !!!");
	refresh();
	getch();
	endwin();

	return 0;
}
