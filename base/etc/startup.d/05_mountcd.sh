#!/bin/esh

if not stat -Lq /dev/cdrom0 then exit 0

echo -n "Mounting CD..." > /dev/pex/splash

insmod /mod/iso9660.ko
mount iso /dev/cdrom0 /cdrom

# Does it look like it might be ours?
if not stat -Lq /cdrom/bootcat then exit 0

echo -e "icon=cd\nrun=cd /cdrom ; exec file-browser\ntitle=CD-ROM" > /home/local/Desktop/5_cdrom.launcher

