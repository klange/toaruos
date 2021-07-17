#!/bin/sh

# Add module descriptions here...
if lspci -q 8086:2415 then insmod /mod/ac97.ko
if lspci -q 1234:1111 then insmod /mod/vmware.ko
if lspci -q 80EE:CAFE then insmod /mod/vbox.ko
if lspci -q 8086:0046 then insmod /mod/i965.ko

if lspci -q 8086:100e,8086:1004,8086:100f,8086:10ea,8086:10d3 then insmod /mod/e1000.ko


