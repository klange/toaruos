#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh
. $DIR/activate.sh

# Just in case
pushd $DIR

VIRTPREFIX=/usr
REALPREFIX=$DIR/../hdd

pushd build
    if [ ! -d newlib-native ]; then
        mkdir newlib-native
    else
        # TOUCHY TOUCHY
        rm -r newlib-native
        mkdir newlib-native
    fi
    pushd $DIR/tarballs/newlib-1.19.0/newlib/libc/sys
        autoconf || bail
        pushd toaru
            autoreconf || bail
            yasm -f elf -o crt0.o crt0.s || bail
            yasm -f elf -o crti.o crti.s || bail
            yasm -f elf -o crtn.o crtn.s || bail
            cp crt0.o ../
            cp crt0.o /tmp/__toaru_crt0.o
            cp crti.o ../
            cp crti.o /tmp/__toaru_crti.o
            cp crtn.o ../
            cp crtn.o /tmp/__toaru_crtn.o
        popd
    popd
    pushd newlib-native
        mkdir -p $TARGET/newlib/libc/sys
        cp /tmp/__toaru_crt0.o $TARGET/newlib/libc/sys/crt0.o
        rm /tmp/__toaru_crt0.o
        cp /tmp/__toaru_crti.o $TARGET/newlib/libc/sys/crti.o
        rm /tmp/__toaru_crti.o
        cp /tmp/__toaru_crtn.o $TARGET/newlib/libc/sys/crtn.o
        rm /tmp/__toaru_crtn.o
        echo "" > $DIR/tarballs/newlib-1.19.0/newlib/libc/stdlib/malign.c
        $DIR/tarballs/newlib-1.19.0/configure --target=$TARGET --prefix=$VIRTPREFIX || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
        cp -r $DIR/patches/newlib/include/* $REALPREFIX/$VIRTPREFIX/include/
        cp $TARGET/newlib/libc/sys/crt0.o $REALPREFIX/$VIRTPREFIX/lib/
        cp $TARGET/newlib/libc/sys/crti.o $REALPREFIX/$VIRTPREFIX/lib/
        cp $TARGET/newlib/libc/sys/crtn.o $REALPREFIX/$VIRTPREFIX/lib/
    popd
    if [ ! -d freetype-native ]; then
        mkdir freetype-native
    fi
    pushd freetype-native
        $DIR/tarballs/freetype-2.4.9/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    # XXX zlib can not be built in a separate directory
    pushd $DIR/tarballs/zlib*
        make distclean
        CC=i686-pc-toaru-gcc ./configure --static --prefix=$VIRTPREFIX || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    if [ ! -d libpng-native ]; then
        mkdir libpng-native
    fi
    pushd libpng-native
        $DIR/tarballs/libpng-1.5.13/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    if [ ! -d pixman-native ]; then
        mkdir pixman-native
    fi
    pushd pixman-native
        $DIR/tarballs/pixman-0.26.2/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    if [ ! -d cairo-native ]; then
        mkdir cairo-native
    fi
    pushd cairo-native
        $DIR/tarballs/cairo-1.12.2/configure --host=$TARGET --prefix=$VIRTPREFIX --enable-ps=no --enable-pdf=no --enable-interpreter=no || bail
        cp $DIR/patches/cairo-Makefile test/Makefile
        cp $DIR/patches/cairo-Makefile perf/Makefile
        echo -e "\n\n#define CAIRO_NO_MUTEX 1" >> config.h
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    # XXX Mesa can not be built from a separate directory (configure script doesn't provide a Makefile)
    pushd $DIR/tarballs/Mesa-*
        make distclean
        ./configure --enable-32-bit --host=$TARGET --prefix=$VIRTPREFIX  --with-osmesa-bits=8 --with-driver=osmesa --disable-egl --disable-shared --without-x --disable-glw --disable-glut --disable-driglx-direct --disable-gallium || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
    popd
    if [ ! -d ncurses-native ]; then
        mkdir ncurses-native
    fi
    pushd ncurses-native
        $DIR/tarballs/ncurses-5.9/configure --prefix=$VIRTPREFIX --host=$TARGET --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo --without-tests || bail
        make || bail
        make DESTDIR=$REALPREFIX install || bail
        cp $DIR/../toaru.tic $REALPREFIX/$VIRTPREFIX/share/terminfo/t/toaru
        cp $DIR/../toaru-vga.tic $REALPREFIX/$VIRTPREFIX/share/terminfo/t/toaru-vga
    popd
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
