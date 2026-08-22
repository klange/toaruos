#!/bin/esh

echo "Mounting tmpfs..." > /dev/console
mount tmpfs tmp,1777 /tmp
mount tmpfs var,755 /var
mkdir /var/run
