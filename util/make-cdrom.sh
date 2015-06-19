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

BLACKLIST='userspace/tests/* userspace/gui/gl/teapot.c'

# Rebuild
echo "Rebuilding..."
rm $BLACKLIST
touch -d tomorrow toaruos-disk.img
make
i686-pc-toaru-strip hdd/bin/*

echo "Cloning CD source directory..."
rm -rf cdrom
cp -r util/cdrom cdrom
mv hdd/mod cdrom/mod

cat > hdd/home/local/.menu.desktop <<EOF
clock,clock-win,Clock
applications-painting,draw,Draw!
julia,julia,Julia Fractals
gears,gears,Gears
drawlines,drawlines,Lines
snow,make-it-snow,Make it Snow
pixman-demo,pixman-demo,Pixman Demo
plasma,plasma,Plasma
applications-simulation,game,RPG Demo
utilities-terminal,terminal,Terminal
ttk-demo,ttk-demo,TTK Demo
EOF

echo "Generating ramdisk..."
genext2fs -B 4096 -d hdd -D util/devtable -U -b 16384 -N 1024 cdrom/ramdisk.img

echo "Installing kernel..."
cp toaruos-kernel cdrom/kernel

echo "Building ISO..."
grub-mkrescue -d /usr/lib/grub/i386-pc -o toaruos.iso cdrom

echo "Restoring modules directory to hdd/mod..."
mv cdrom/mod hdd/mod
rm -r cdrom
git checkout $BLACKLIST
