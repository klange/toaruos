#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh
. $DIR/activate.sh

VIRTPREFIX=/usr
REALPREFIX=$DIR/../hdd

pushd tarballs > /dev/null
    rm -r "ncurses-5.9"
    deco "ncurses" "ncurses-5.9.tar.gz"
    patc "ncurses" "ncurses-5.9"
popd > /dev/null

pushd build

    if [ ! -d ncurses-native ]; then
        mkdir ncurses-native
    fi
    pushd ncurses-native
        $DIR/tarballs/ncurses-5.9/configure --prefix=$VIRTPREFIX --host=$TARGET --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo --without-tests || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
        cp $DIR/../util/toaru.tic $REALPREFIX/$VIRTPREFIX/share/terminfo/t/toaru
        cp $DIR/../util/toaru-vga.tic $REALPREFIX/$VIRTPREFIX/share/terminfo/t/toaru-vga
    popd

    pushd $REALPREFIX$VIRTPREFIX/bin || bail
        $TARGET-strip *
    popd

popd
