#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh
. $DIR/activate.sh

VIRTPREFIX=/usr
REALPREFIX=$DIR/../hdd

pushd tarballs > /dev/null
    rm -rf "vim73"
    deco "vim" "vim-7.3.tar.bz2"
    patc "vim" "vim73"
popd > /dev/null

pushd build

    pushd $DIR/tarballs/vim73
        make distclean
        ac_cv_sizeof_int=4 vim_cv_getcwd_broken=no vim_cv_memmove_handles_overlap=yes vim_cv_stat_ignores_slash=no vim_cv_tgetent=zero vim_cv_terminfo=yes vim_cv_toupper_broken=no vim_cv_tty_group=world ./configure --host=$TARGET --target=$TARGET --prefix=$VIRTPREFIX --with-tlib=ncurses --enable-gui=no --disable-gtktest --disable-xim --with-features=normal --disable-gpm --without-x --disable-netbeans --enable-multibyte
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd

    pushd $REALPREFIX$VIRTPREFIX/bin || bail
        $TARGET-strip *
    popd

popd
