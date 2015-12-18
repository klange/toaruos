#!/bin/bash

if [[ $EUID -ne 0 ]]; then
    echo -e "\033[1;31mYou're going to need to run this as root\033[0m" 1>&2
    echo "Additionally, verify that /dev/loop4 is available and that" 1>&2
    echo "/mnt is available for mounting; otherwise, modify the script" 1>&2
    echo "to use alternative loop devices or mount points as needed." 1>&2
    exit 1
fi

if [[ $# -lt 1 ]]; then
    echo "I need a path to a compiled とあるOS source directory as an argument, try again." 1>&2
    exit 1
fi

DISK=toaru-disk.img
SRCDIR=$1
BOOT=./boot
SIZE=1G

echo "Please select a disk size."
read -p "[1ml[0m for 1GB, [1ms[0m for 256MB: "
if [ "$REPLY" == "small" ] ; then
    SIZE=256M
fi

echo "I will create partitioned, ext2 disk image of size $SIZE x 4KB at $DISK from files in $SRCDIR as well as boot scripts in $BOOT"
read -p "Is this correct? (Y/n)"
if [ "$REPLY" == "n" ] ; then
    echo "Oh, okay, never mind then."
    exit
fi

type kpartx >/dev/null 2>&1 || { echo "Trying to install kpartx..."; apt-get install kpartx; }

# Create a 1GiB blank disk image.
dd if=/dev/zero of=$DISK bs=$SIZE count=1

echo "Partitioning..."

cat parted.conf | parted $DISK

echo "Done partition."


# Here's where we need to be root.
LOOPRAW=`losetup -f`
losetup $LOOPRAW $DISK
TMP=`kpartx -av $DISK`
TMP2=${TMP/add map /}
LOOP=${TMP2%%p1 *}
LOOPDEV=/dev/${LOOP}
LOOPMAP=/dev/mapper/${LOOP}p1

if [ ! -e $LOOPDEV ] ; then
    echo "Bailing! $LOOPDEV is not valid"
    exit
fi

if [ ! -e $LOOPMAP ] ; then
    echo "Bailing! $LOOPMAP is not valid"
    exit
fi

mkfs.ext2 ${LOOPMAP}

mount ${LOOPMAP} /mnt

echo "Installing main files."
cp -r $SRCDIR/hdd/* /mnt/

echo "Installing boot files."
mkdir -p  /mnt/boot
cp -r $BOOT/* /mnt/boot/

echo "Installing kernel."
cp -r $SRCDIR/toaruos-kernel /mnt/boot/

echo "Installing grub."
grub-install --target=i386-pc --boot-directory=/mnt/boot $LOOPRAW

echo "Cleaning up"
umount /mnt
kpartx -d ${LOOPMAP}
dmsetup remove ${LOOPMAP}
losetup -d ${LOOPDEV}
losetup -d ${LOOPRAW}

if [ -n "$SUDO_USER" ] ; then
    echo "Reassigning permissions on disk image to $SUDO_USER"
    chown $SUDO_USER:$SUDO_USER $DISK
fi


echo "Done. You can boot the disk image with qemu now."
