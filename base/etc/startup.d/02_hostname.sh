#!/bin/esh

export-cmd HOSTNAME cat /etc/hostname

echo "Setting hostname..." > /dev/console

if [ -z "$HOSTNAME" ] then exec hostname "localhost" else exec hostname "$HOSTNAME"
