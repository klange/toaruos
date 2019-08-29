#!/bin/bash
# NOTE: This list is manually compiled, and should catch all ncurses files.
# This takes a lot of work, must be a better way

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TOARU_ROOT="$DIR/../../base"
PKG_PREFIX=/usr

rm -rv $TOARU_ROOT$PKG_PREFIX/bin/captoinfo
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/clear
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/infocmp
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/infotocap
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/ncurses6-config
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/reset
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/tabs
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/tic
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/toe
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/tput
rm -rv $TOARU_ROOT$PKG_PREFIX/bin/tset
rm -rv $TOARU_ROOT$PKG_PREFIX/include/curses.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/eti.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/form.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/menu.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/nc_tparm.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/ncurses_dll.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/ncurses.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/panel.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/termcap.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/term_entry.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/term.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/tic.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/unctrl.h
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libcurses.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libform.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libform_g.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libmenu.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libmenu_g.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libncurses.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libncurses_g.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libpanel.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libpanel_g.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/pkgconfig/form.pc
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/pkgconfig/menu.pc
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/pkgconfig/ncurses.pc
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/pkgconfig/panel.pc
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/terminfo
rm -rv $TOARU_ROOT$PKG_PREFIX/share/tabset
rm -rv $TOARU_ROOT$PKG_PREFIX/share/terminfo
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/captoinfo.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/clear.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/infocmp.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/infotocap.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/ncurses6-config.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/reset.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/tabs.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/tic.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/toe.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/tput.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/tset.1.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/*3ncurses*
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/*3form*
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/*3menu*
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/*3curses*
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man5/scr_dump.5.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man5/term.5.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man5/terminfo.5.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man5/user_caps.5.gz
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man7/term.7.gz
