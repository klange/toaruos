#!/bin/esh

if kcmdline -q no-startup-dhcp then exit 0

echo -n "Setting up network..." >> /dev/pex/splash
/bin/dhclient
