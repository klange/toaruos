#!/bin/esh

if kcmdline -q no-startup-dhcp then exit 0

echo "Setting up network..." > /dev/console
/bin/dhclient
