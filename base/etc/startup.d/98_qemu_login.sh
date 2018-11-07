#!/bin/sh

if not qemu-fwcfg -q opt/org.toaruos.forceuser then exit 0

export-cmd TERM qemu-fwcfg opt/org.toaruos.term
export-cmd QEMU_USER qemu-fwcfg opt/org.toaruos.forceuser
/bin/getty -a "$QEMU_USER"
reboot

