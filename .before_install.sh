#!/bin/bash

# Travis before_install script
sudo apt-get update
sudo apt-get install clang yasm genext2fs build-essential wget libmpfr-dev libmpc-dev libgmp-dev qemu autoconf automake texinfo
sudo apt-get remove kvm-ipxe

