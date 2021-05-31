DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TARGET=x86_64-pc-toaru
PREFIX="$DIR/local"
SYSROOT="$DIR/../base"

cd $DIR
mkdir -p $PREFIX/bin
gcc -I$DIR/../kuroko/src -DNO_RLINE -DSTATIC_ONLY -DKRK_DISABLE_THREADS -o "$PREFIX/bin/kuroko" $DIR/../kuroko/src/*.c

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

cd $DIR/../
make base/lib/libc.so

cd $DIR/build/gcc
make -j8 all-target-libgcc
make install-target-libgcc

cd $DIR/../
make base/lib/libm.so

cd $DIR/build/gcc
make -j8 all-target-libstdc++-v3
make install-target-libstdc++-v3
