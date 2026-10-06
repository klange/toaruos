#!/bin/esh
echo "Mounting procfs" >/dev/console
mkdir -m 0555 /proc
mount procfs "" /proc
