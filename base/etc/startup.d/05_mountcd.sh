#!/bin/sh

if not stat -Lq /dev/cdrom0 then exit 0

echo -n "Mounting CD..." > /dev/pex/splash

mount iso /dev/cdrom0 /cdrom
