#!/bin/bash
# Maybe look into dependencies and a master build script?

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PKG_NAME=file
PKG_VERSION=5.37
PKG_URL=http://mirror.jaleco.com/lfs/pub/lfs/lfs-packages/9.0-rc1/file-5.37.tar.gz
PKG_TARBALL=$PKG_NAME-$PKG_VERSION.tar.gz
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

# Prepare the source
cd $PKG_ARCHIVE_DIR
patch -p1 < ../$PKG_NAME.patch || exit 1

# Configure and build the program
./configure --host=i686-pc-toaru --prefix=$PKG_PREFIX || exit 1
make || exit 1

# Install it
#make DESTDIR=/root/FS-TOARU-STORAGE/file install || exit 1
make DESTDIR=$TOARU_ROOT install || exit 1
cd ..
