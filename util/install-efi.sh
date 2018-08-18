#!/bin/bash

cp fatbase/efi/boot/bootx64.efi /boot/efi/EFI/toaru/toarux64.efi
cp fatbase/{ramdisk.img,kernel} /boot/efi/
mkdir -p /boot/efi/mod
cp fatbase/mod/* /boot/efi/mod/
