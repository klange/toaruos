#!/bin/bash
# NOTE: This list is manually compiled, and should catch all zlib files.
# This takes a lot of work, must be a better way

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TOARU_ROOT="$DIR/../../base"
PKG_PREFIX=/usr

rm -rv $TOARU_ROOT$PKG_PREFIX/include/zconf.h
rm -rv $TOARU_ROOT$PKG_PREFIX/include/zlib.h
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libz.a
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libz.so
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libz.so.1
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/libz.so.1.2.11
rm -rv $TOARU_ROOT$PKG_PREFIX/lib/pkgconfig/zlib.pc
rm -rv $TOARU_ROOT$PKG_PREFIX/share/man/man3/zlib.3
