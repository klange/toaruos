#!/bin/bash

RET=0

if ! which python3 >/dev/null; then
    echo "python3 is required to run build tools - 3.6 is recommended as it is needed to cross-compile itself"
    RET=1
fi

if ! which genext2fs >/dev/null; then
    echo "genext2fs is needed to build ramdisk images"
    RET=1
else
    if [ -z "$(genext2fs --help 2>&1 | grep -- "block-size")" ]; then
        echo "genext2fs must support the -B (--block-size) argument; try building with Debian patches"
        RET=1
    fi
fi

if ! which mkfs.fat >/dev/null; then
    echo "mkfs.fat is required (and should be in your PATH) to build EFI file systems"
    RET=1
fi

if ! which mcopy >/dev/null; then
    echo "mtools is required to build FAT images for EFI / hybrid ISOs"
    RET=1
fi

if ! which xorriso >/dev/null; then
    echo "xorriso is required to build ISO CD images"
    RET=1
fi

if ! which yasm >/dev/null; then
    echo "yasm is required to build some assembly sources"
    RET=1
fi

if ! which autoconf >/dev/null; then
    echo "autoconf is required to build GCC cross-compiler"
    RET=1
fi

if ! which automake >/dev/null; then
    echo "automake is required to build GCC cross-compiler"
    RET=1
fi

if ! which wget >/dev/null; then
    echo "wget is required to build GCC cross-compiler"
    RET=1
fi

if [ ! -e /usr/lib32/crt0-efi-ia32.o ]; then
    echo "gnu-efi is required to build EFI loaders"
    RET=1
fi

if ! cpp <(echo "#include \"gmp.h\"") >/dev/null 2>/dev/null; then
    echo "GMP headers are required to build GCC cross-compiler"
    RET=1
fi

if ! cpp <(echo "#include \"mpfr.h\"") >/dev/null 2>/dev/null; then
    echo "MPFR headers are required to build GCC cross-compiler"
    RET=1
fi

if ! cpp <(echo "#include \"mpfr.h\"") >/dev/null 2>/dev/null; then
    echo "MPFR headers are required to build GCC cross-compiler"
    RET=1
fi

if ! cpp <(echo "#include \"mpc.h\"") >/dev/null 2>/dev/null; then
    echo "MPC headers are required to build GCC cross-compiler"
    RET=1
fi

exit $RET

