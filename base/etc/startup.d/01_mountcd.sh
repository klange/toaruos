#!/bin/sh

if stat -Lq /dev/cdrom0 then mount iso /dev/cdrom0 /cdrom
