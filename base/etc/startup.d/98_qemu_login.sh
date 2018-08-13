#!/bin/sh

export-cmd QEMU_USER qemu-fwcfg opt/org.toaruos.forceuser

if empty? "$QEMU_USER" then true else /bin/getty -a "$QEMU_USER"
if empty? "$QEMU_USER" then true else reboot

