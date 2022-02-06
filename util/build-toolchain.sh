DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ARCH=x86_64
TARGET=x86_64-pc-toaru
PREFIX="$DIR/local"
SYSROOT="$DIR/../base"

# --disable-multilib

cd $DIR
mkdir -p $PREFIX/bin

mkdir -p $DIR/build/binutils
cd $DIR/build/binutils
../../binutils-gdb/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" --disable-werror --enable-shared
make -j8
make install

mkdir -p $DIR/build/gcc
cd $DIR/build/gcc
../../gcc/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" --enable-languages=c,c++ --enable-shared
make -j8 all-gcc
make install-gcc
make -j8 $TARGET/libgcc/{libgcc.a,crtbegin.o,crtend.o,crtbeginS.o,crtendS.o}
cp $TARGET/libgcc/{libgcc.a,crtbegin.o,crtend.o,crtbeginS.o,crtendS.o} ../../local/lib/gcc/$TARGET/10.3.0/

cd $DIR/../
make ARCH=$ARCH base/lib/libc.so

cd $DIR/build/gcc
make -j8 all-target-libgcc
make install-target-libgcc

cd $DIR/../
rm base/lib/libc.so
make ARCH=$ARCH base/lib/libc.so
make ARCH=$ARCH base/lib/libm.so

#cd $DIR/build/gcc
#make -j8 all-target-libstdc++-v3
#make install-target-libstdc++-v3
