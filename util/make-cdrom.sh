#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Ensure we're in the root of the git repo.
cd $DIR/..

# Check that we're trying to build with our own toolchain,
# as it installs things to hdd/ and we're going to reset
# that directory to a clean slate (which will lose, eg.
# includes, libraries, etc.)
if [[ $TOOLCHAIN/ = $PWD/* ]]; then
	echo "You should not try to build CD ISOs from your active"
	echo "development repository, as the process is destructive"
	echo "in its attempts to build small ramdisks."
	echo ""
	echo "Instead, create a new clone of your repository,"
	echo "activate your development toolchain, and then run this"
	echo "script again."
	exit 1
fi

# Rebuild
echo "Rebuilding..."
rm userspace/tests/*
touch -d tomorrow toaruos-disk.img
make
i686-pc-toaru-strip hdd/bin/*

echo "Cloning CD source directory..."
rm -rf cdrom
cp -r util/cdrom cdrom
mv hdd/mod cdrom/mod

echo "Generating ramdisk..."
genext2fs -B 4096 -d hdd -D util/devtable -U -b 16384 -N 1024 cdrom/ramdisk.img

echo "Installing kernel..."
cp toaruos-kernel cdrom/kernel

echo "Building ISO..."
grub-mkrescue -d /usr/lib/grub/i386-pc -o toaruos.iso cdrom

echo "Restoring modules directory to hdd/mod..."
mv cdrom/mod hdd/mod
rm -r cdrom
git co userspace/tests/
