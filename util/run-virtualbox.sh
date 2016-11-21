#!/bin/bash
# Initialize a VirtualBox VM pointing towards a dev hard disk, with grub from a piggyback CD.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIR/..

VMNAME="ToaruOS Dev HDD"


echo -e "\e[1;32m>>> Generating bootstrap CD...\e[0m"
# Copy the CD directory like we do when building a normal CD
rm -rf vbox-cdrom
mkdir vbox-cdrom
mkdir vbox-cdrom/boot
mkdir vbox-cdrom/boot/grub
cat > vbox-cdrom/boot/grub/grub.cfg <<END
set root='(hd0)'

set timeout=0

function load_modules {
echo "Loading modules..."
module /mod/zero.ko
module /mod/random.ko
module /mod/serial.ko
module /mod/debug_shell.ko
module /mod/procfs.ko
module /mod/tmpfs.ko
module /mod/ata.ko
module /mod/ext2.ko
module /mod/iso9660.ko
module /mod/ps2kbd.ko
module /mod/ps2mouse.ko
module /mod/lfbvideo.ko
module /mod/vboxguest.ko
module /mod/vidset.ko
module /mod/packetfs.ko
module /mod/snd.ko
module /mod/ac97.ko
module /mod/net.ko
module /mod/pcnet.ko
}

submenu 'Go' {
multiboot (cd)/kernel vid=auto root=/dev/hda
load_modules
boot
}

configfile /boot/grub/menus.cfg
END

cp toaruos-kernel vbox-cdrom/kernel

grub-mkrescue -d /usr/lib/grub/i386-pc -o vbox-boot.iso vbox-cdrom
rm -rf vbox-cdrom

echo -e "\e[1;32m>>> Creating virtual machine...\e[0m"
VBoxManage unregistervm "$VMNAME" --delete
VBoxManage internalcommands createrawvmdk -filename vbox-disk.vmdk -rawdisk $DIR/../toaruos-disk.img
VBoxManage createvm --name "$VMNAME" --ostype "Other" --register
VBoxManage modifyvm "$VMNAME" --memory 1024 --audio pulse --audiocontroller ac97
VBoxManage storagectl "$VMNAME" --add ide --name "IDE"
VBoxManage storageattach "$VMNAME" --storagectl "IDE" --port 0 --device 0 --medium `pwd`/vbox-disk.vmdk --type hdd
VBoxManage storageattach "$VMNAME" --storagectl "IDE" --port 1 --device 0 --medium `pwd`/vbox-boot.iso --type dvddrive

echo -e "\e[1;32m>>> Starting virtual machine...\e[0m"
VBoxManage startvm "$VMNAME"

echo -e "\e[1;32m>>> Waiting for virtual machine to shut down...\e[0m"
# Wait for VM to shut down
until $(VBoxManage showvminfo --machinereadable "$VMNAME" | grep -q ^VMState=.poweroff.)
do
    sleep 1
done

echo -e "\e[1;32m>>> Cleaning up...\e[0m"

sleep 1
# Clean up
VBoxManage unregistervm "$VMNAME" --delete # This also removes the vmdk we made
rm vbox-boot.iso


