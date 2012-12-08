#!/bin/bash

# Travis before_script
unset CC

sudo mkdir -p /home/build/osdev/toolchain

sudo chmod a=rwx /home/build
sudo chmod a=rwx /home/build/osdev
sudo chmod a=rwx /home/build/osdev/toolchain

pushd toolchain
    cp *.sh /home/build/osdev/toolchain/
popd

pushd /home/build/osdev/toolchain
    wget "https://github.com/downloads/klange/osdev/toolchain-2012-12-07.tar.gz"
    tar -xaf "toolchain-2012-12-07.tar.gz"
    . activate.sh || exit 1
    echo $PATH
    $TARGET-gcc --version
popd
