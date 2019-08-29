DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PKG_NAME=ncurses
PKG_VERSION=6.1
PKG_URL=https://ftp.gnu.org/gnu/ncurses/ncurses-$PKG_VERSION.tar.gz
PKG_TARBALL=$PKG_NAME-$PKG_VERSION.tar.gz
PKG_ARCHIVE_DIR=$PKG_NAME-$PKG_VERSION
PKG_PREFIX=/usr
TOARU_ROOT="$DIR/../../base"
export PKG_CONFIG_SYSROOT_DIR=$TOARU_ROOT
export PKG_CONFIG_LIBDIR=$TOARU_ROOT/usr/lib/pkgconfig:$TOARU_ROOT/usr/share/pkgconfig
#echo "$TOARU_ROOT"
#echo "$PKG_CONFIG_SYSROOT_DIR"
#echo "$PKG_CONFIG_LIBDIR"
#PKG_DEPS="mlibc"

#pkg_fetch() {
    [ -d $PKG_ARCHIVE_DIR ] && rm -rv $PKG_ARCHIVE_DIR
    [ -f $PKG_TARBALL ] || wget $PKG_URL
    [ -d $PKG_ARCHIVE_DIR ] || tar -xf $PKG_TARBALL

    cd $PKG_ARCHIVE_DIR
    patch -p1 < ../$PKG_NAME.patch || exit 1
#}

#pkg_build() {
    #cd $PKG_ARCHIVE_DIR
    ./configure --host=i686-pc-toaru --prefix=$PKG_PREFIX --without-tests || exit 1
    make || exit 1
#}

#pkg_install() {
    #cd $PKG_ARCHIVE_DIR
    make DESTDIR=$TOARU_ROOT install || exit 1
    #make DESTDIR=/root/FS-TOARU-STORAGE/ncurses install || exit 1
    cd ..
#}

#pkg_clean() {
#    rm -rf $PKG_ARCHIVE_DIR
#}