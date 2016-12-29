#!/bin/bash

if [ -d toolchain/local/bin ]; then
    echo 'You have not activated your toolchain (. toolchain/activate.sh)'
elif [ -e .toolchain ]; then
    echo "You have not activated your toolchain (. `cat .toolchain | sed 's%hdd/usr%toolchain/activate.sh%'`)"
else
    echo 'No toolchain found. Did you mean to run `make toolchain` or source an existing toolchain?'
fi
