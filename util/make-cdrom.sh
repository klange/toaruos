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

BLACKLIST="userspace/tests/* hdd/usr/share/wallpapers/{grandcanyon,paris,southbay,yokohama,yosemite}.png"

# Rebuild
echo "Rebuilding... (ignore warnings about time skew, this is intentional)"
eval rm $BLACKLIST
touch -d tomorrow toaruos-disk.img
make STRIP_LIBS=1
i686-pc-toaru-strip hdd/bin/*

echo "Cloning CD source directory..."
rm -rf cdrom
cp -r util/cdrom cdrom
mv hdd/mod _mod_tmp
mkdir cdrom/mod
cp _mod_tmp/* cdrom/mod/
xz cdrom/mod/*


mkdir -p hdd/usr/share/terminfo/t
cp util/toaru.tic hdd/usr/share/terminfo/t/toaru

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
xz cdrom/ramdisk.img

echo "Installing kernel..."
cp toaruos-kernel cdrom/kernel
xz cdrom/kernel

echo "Building ISO..."
if grep precise /etc/lsb-release; then
	# Hack for travis build hosts (old grub-mkrescue, no -d)
	grub-mkrescue -o toaruos.iso cdrom
else
	grub-mkrescue -d /usr/lib/grub/i386-pc --compress=xz -o toaruos.iso cdrom
fi

echo "Restoring modules directory to hdd/mod..."
mv _mod_tmp hdd/mod
rm -r cdrom
eval git checkout $BLACKLIST
