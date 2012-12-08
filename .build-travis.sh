#!/bin/bash

unset CC
. toolchain/activate.sh || exit 1
pushd userspace
make || exit 1
popd
make system || exit 1
make test || exit 1
