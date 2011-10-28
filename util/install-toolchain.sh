#!/bin/bash

TOOLCHAIN=`which i686-pc-toaru-gcc`
if [[ -n "$TOOLCHAIN" ]] ; then
    echo "Toolchain is present at $TOOLCHAIN"
    exit 0
fi

echo -e "\033[1mYou do not appear to have a toolchain available for とあるOS (i686-pc-toaru-gcc was not found in your path).\033[0m"
echo -e "\033[1mI can retreive a pre-built copy of the toolchain for Linux (32-bit, approx. 300MB)\033[0m"
echo -en "\033[1;32mWould you like me to do that?\033[0m (Y/n) "

read PROMPT

if [[ "$PROMPT" = "y" ]] ; then
    echo "The toolchain will be pulled from Dropbox: http://dl.dropbox.com/u/44305966/toaru-toolchain-0.0.1.tar.gz"
    pushd /tmp
    wget "http://dl.dropbox.com/u/44305966/toaru-toolchain-0.0.1.tar.gz"
    popd
    tar -x -C util/ -f "/tmp/toaru-toolchain-0.0.1.tar.gz"
    echo "Done!"
    echo "You should now add \033[1;33m`pwd`/util/toaru-toolchain/bin\033[0m to your path"
else
    echo "I will not retreive the toolchain. Bailing..."
    exit 1
fi
