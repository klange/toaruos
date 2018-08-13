#!/bin/sh

if qemu-fwcfg -q opt/org.toaruos.forceuser then true else exit 0

export-cmd QEMU_USER qemu-fwcfg opt/org.toaruos.forceuser
/bin/getty -a "$QEMU_USER"
reboot

