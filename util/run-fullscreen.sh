#!/bin/bash
# Script to start at full screen resolution

RAM=1024
KVM="-enable-kvm"
FULLSCREEN="-no-frame"
RESOLUTION=`xrandr -q|perl -F'\s|,' -lane "/^Sc/&&print join '',@F[8..10]" | sed 's/x/=/'`

qemu-system-i386 -kernel toaruos-kernel -m $RAM -k en-us -append "vid=qemu=$RESOLUTION hdd" -serial stdio -vga std -hda toaruos-disk.img $KVM $FULLSCREEN
