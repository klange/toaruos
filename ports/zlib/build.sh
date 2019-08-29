#!/bin/bash
# Maybe look into dependencies and a master build script?

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PKG_NAME=zlib
PKG_VERSION=1.2.11
PKG_URL=https://github.com/madler/$PKG_NAME/archive/v$PKG_VERSION.tar.gz
PKG_TARBALL=v$PKG_VERSION.tar.gz
PKG_ARCHIVE_DIR=$PKG_NAME-$PKG_VERSION
PKG_PREFIX=/usr
TOARU_ROOT="$DIR/../../base"
export PKG_CONFIG_SYSROOT_DIR=$TOARU_ROOT
export PKG_CONFIG_LIBDIR=$TOARU_ROOT/usr/lib/pkgconfig:$TOARU_ROOT/usr/share/pkgconfig

# Get the source, remove the untarred directory if it exists, grab the tarball if it does not exists
# and extract the tarball
[ -d $PKG_ARCHIVE_DIR ] && rm -rv $PKG_ARCHIVE_DIR
[ -f $PKG_TARBALL ] || wget $PKG_URL
[ -d $PKG_ARCHIVE_DIR ] || tar -xf $PKG_TARBALL

# Prepare the source, there is no patch for this package
cd $PKG_ARCHIVE_DIR
#patch -p1 < ../$PKG_NAME.patch || exit 1

# Configure and build the program
CHOST=i686-pc-toaru ./configure --prefix=$PKG_PREFIX || exit 1
make || exit 1

# Install it
#make DESTDIR=/root/FS-TOARU-STORAGE install || exit 1
make DESTDIR=$TOARU_ROOT install || exit 1
cd ..
