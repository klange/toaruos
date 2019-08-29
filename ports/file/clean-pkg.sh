#!/bin/bash
# NOTE: This list is manually compiled, and should catch all ncurses files.
# This takes a lot of work, must be a better way

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TOARU_ROOT="$DIR/../../base"
PKG_PREFIX=/usr

rm -rv $TOARU_ROOT$PKG_PREFIX/bin/file
rm -rv $TOARU_ROOT$PKG_PREFIX/include/magic.h
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libmagic.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libmagic.la
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man1/file.1
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/libmagic.3
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man4/magic.4
rm -rv $TOARU_ROOT$PKG_PREFIX/share/misc/magic.mgc
