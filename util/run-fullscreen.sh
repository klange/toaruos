#!/bin/bash
# Script to start at full screen resolution

RAMDISK="-initrd toaruos-initrd"
RAM=512
KVM="-enable-kvm"
FULLSCREEN="-full-screen"

qemu -kernel toaruos-kernel -m $RAM $RAMDISK -append "vid=qemu,`xrandr -q|perl -F'\s|,' -lane "/^Sc/&&print join '',@F[8..10]" | sed 's/x/,/'` hdd" -serial stdio -vga std -hda toaruos-disk.img $KVM $FULLSCREEN
