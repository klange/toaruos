#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

. $DIR/config.sh
. $DIR/util.sh

# Build everything by default.
BUILD_BINUTILS=true
BUILD_GCC=true
BUILD_NEWLIB=true
BUILD_LIBSTDCPP=true
BUILD_ZLIB=true
BUILD_FREETYPE=true
BUILD_PNG=true
BUILD_PIXMAN=true
BUILD_CAIRO=true
BUILD_MESA=true
BUILD_NCURSES=true
BUILD_VIM=true

#BUILD_BINUTILS=false
#BUILD_GCC=false
#BUILD_NEWLIB=false
#BUILD_LIBSTDCPP=false
#BUILD_ZLIB=false
#BUILD_FREETYPE=false
#BUILD_PNG=false
#BUILD_PIXMAN=false
#BUILD_CAIRO=false
#BUILD_MESA=false
#BUILD_NCURSES=false
#BUILD_VIM=false

echo "Building a toolchain with a sysroot of $TOARU_SYSROOT with host binaries in $PREFIX targeting $TARGET"

if [ ! -d build ]; then
    mkdir build
fi

pushd build
    if $BUILD_BINUTILS; then
        if [ ! -d binutils ]; then
            mkdir binutils
        fi

        unset PKG_CONFIG_LIBDIR

        pushd binutils
            $DIR/tarballs/binutils-2.22/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$TOARU_SYSROOT --disable-werror || bail
            make || bail
            make install || bail
        popd
    fi

    if $BUILD_GCC; then
        if [ -d gcc ]; then
            rm -rf gcc
        fi
        mkdir gcc

        unset PKG_CONFIG_LIBDIR

        pushd gcc
            $DIR/tarballs/gcc-4.6.4/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$TOARU_SYSROOT --with-build-sysroot=$TOARU_SYSROOT --with-native-system-header-dir=$TOARU_SYSROOT --disable-nls --enable-languages=c,c++ --disable-libssp --with-newlib || bail
            make all-gcc || bail
            make install-gcc || bail
            make all-target-libgcc || bail
            make install-target-libgcc || bail
        popd
    fi

    . $DIR/activate.sh

    if $BUILD_NEWLIB; then
        if [ ! -d newlib ]; then
            mkdir newlib
        else
            # Newlib is touchy about reconfigures
            rm -r newlib
            mkdir newlib
        fi
        pushd $DIR/tarballs/newlib-1.19.0
            find -type f -exec sed 's|--cygnus||g;s|cygnus||g' -i {} + || bail
        popd
        pushd $DIR/tarballs/newlib-1.19.0/newlib/libc/sys
            autoconf || bail
            pushd toaru
                touch INSTALL NEWS README AUTHORS ChangeLog COPYING || bail
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
        pushd newlib
            mkdir -p $TARGET/newlib/libc/sys
            cp /tmp/__toaru_crt0.o $TARGET/newlib/libc/sys/crt0.o
            rm /tmp/__toaru_crt0.o
            cp /tmp/__toaru_crti.o $TARGET/newlib/libc/sys/crti.o
            rm /tmp/__toaru_crti.o
            cp /tmp/__toaru_crtn.o $TARGET/newlib/libc/sys/crtn.o
            rm /tmp/__toaru_crtn.o
            echo "" > $DIR/tarballs/newlib-1.19.0/newlib/libc/stdlib/malign.c
            $DIR/tarballs/newlib-1.19.0/configure --target=$TARGET --prefix=$VIRTPREFIX || bail
            # Fix the damned tooldir
            sed -s 's/prefix}\/i686-pc-toaru/prefix}/' Makefile > Makefile.tmp
            mv Makefile.tmp Makefile
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
            cp -r $DIR/patches/newlib/include/* $TOARU_SYSROOT/$VIRTPREFIX/include/
            cp $TARGET/newlib/libc/sys/crt0.o $TOARU_SYSROOT/$VIRTPREFIX/lib/
            cp $TARGET/newlib/libc/sys/crti.o $TOARU_SYSROOT/$VIRTPREFIX/lib/
            cp $TARGET/newlib/libc/sys/crtn.o $TOARU_SYSROOT/$VIRTPREFIX/lib/
        popd
    fi

    if $BUILD_LIBSTDCPP; then
        pushd gcc
            # build libstdc++
            make all-target-libstdc++-v3 || bail
            make install-target-libstdc++-v3 || bail
        popd
    fi

    if $BUILD_FREETYPE; then
        if [ ! -d freetype ]; then
            mkdir freetype
        fi
        pushd freetype
            $DIR/tarballs/freetype-2.4.9/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_ZLIB; then
        # XXX zlib can not be built in a separate directory
        pushd $DIR/tarballs/zlib*
            CC=i686-pc-toaru-gcc ./configure --static --prefix=$VIRTPREFIX || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_PNG; then
        if [ ! -d libpng ]; then
            mkdir libpng
        fi
        pushd libpng
            $DIR/tarballs/libpng-1.5.13/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_PIXMAN; then
        if [ ! -d pixman ]; then
            mkdir pixman
        fi
        pushd pixman
            $DIR/tarballs/pixman-0.26.2/configure --host=$TARGET --prefix=$VIRTPREFIX || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_CAIRO; then
        if [ ! -d cairo ]; then
            mkdir cairo
        fi
        pushd cairo
            $DIR/tarballs/cairo-1.12.2/configure --host=$TARGET --prefix=$VIRTPREFIX --enable-ps=no --enable-pdf=no --enable-interpreter=no --enable-xlib=no || bail
            cp $DIR/patches/cairo-Makefile test/Makefile
            cp $DIR/patches/cairo-Makefile perf/Makefile
            echo -e "\n\n#define CAIRO_NO_MUTEX 1" >> config.h
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_MESA; then
        # XXX Mesa can not be built from a separate directory (configure script doesn't provide a Makefile)
        pushd $DIR/tarballs/Mesa-*
            ./configure --enable-32-bit --host=$TARGET --prefix=$VIRTPREFIX  --with-osmesa-bits=8 --with-driver=osmesa --disable-egl --disable-shared --without-x --disable-glw --disable-glut --disable-driglx-direct --disable-gallium || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    if $BUILD_NCURSES; then
        if [ ! -d ncurses ]; then
            mkdir ncurses
        fi
        pushd ncurses
            $DIR/tarballs/ncurses-5.9/configure --prefix=$VIRTPREFIX --host=$TARGET --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo --without-tests || bail
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
            cp $DIR/../util/toaru.tic $TOARU_SYSROOT/$VIRTPREFIX/share/terminfo/t/toaru
            cp $DIR/../util/toaru-vga.tic $TOARU_SYSROOT/$VIRTPREFIX/share/terminfo/t/toaru-vga
        popd
    fi

    if $BUILD_VIM; then
        pushd $DIR/tarballs/vim73
            make distclean
            ac_cv_sizeof_int=4 vim_cv_getcwd_broken=no vim_cv_memmove_handles_overlap=yes vim_cv_stat_ignores_slash=no vim_cv_tgetent=zero vim_cv_terminfo=yes vim_cv_toupper_broken=no vim_cv_tty_group=world ./configure --host=$TARGET --target=$TARGET --with-sysroot=$TOARU_SYSROOT --prefix=$VIRTPREFIX --with-tlib=ncurses --enable-gui=no --disable-gtktest --disable-xim --with-features=normal --disable-gpm --without-x --disable-netbeans --enable-multibyte
            make || bail
            make DESTDIR=$TOARU_SYSROOT install || bail
        popd
    fi

    pushd $TOARU_SYSROOT/usr/bin || bail
        $TARGET-strip *
    popd

popd
