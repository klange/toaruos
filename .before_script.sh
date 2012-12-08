#!/bin/bash

# Travis before_script
unset CC

pushd toolchain
./prepare.sh  || exit 1
./install.sh  > /dev/null 2> /dev/null || exit 1
. activate.sh || exit 1
popd
