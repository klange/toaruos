#!/bin/esh

echo -n "Installing device driver modules..." > /dev/pex/splash

# Only load this in virtualbox for now, as we're not
# even sure we're doing the remapping correctly...
if lspci -q 80EE:CAFE,8086:7000 then insmod /mod/piix4.ko

# Add module descriptions here...
if lspci -q 8086:2415 then insmod /mod/ac97.ko
if lspci -q 1234:1111,15ad:07a0 then insmod /mod/vmware.ko
if lspci -q 80EE:CAFE then insmod /mod/vbox.ko
if lspci -q 8086:0046 then insmod /mod/i965.ko
if lspci -q 1274:1371 then insmod /mod/es1371.ko

if lspci -q 8086:100e,8086:1004,8086:100f,8086:10ea,8086:10d3 then insmod /mod/e1000.ko

# Device drivers
if lspci -q 8086:7111,8086:7010 then insmod /mod/ata.ko
