#!/bin/bash
# Initialize a VirtualBox VM pointing towards a dev hard disk, with grub from a piggyback CD.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIR/..

VMNAME="ToaruOS CD"

echo -e "\e[1;32m>>> Creating virtual machine...\e[0m"
VBoxManage unregistervm "$VMNAME" --delete
VBoxManage createvm --name "$VMNAME" --ostype "Other" --register
VBoxManage modifyvm "$VMNAME" --memory 1024 --audio pulse --audiocontroller ac97
VBoxManage storagectl "$VMNAME" --add ide --name "IDE"
VBoxManage storageattach "$VMNAME" --storagectl "IDE" --port 0 --device 0 --medium `pwd`/toaruos.iso --type dvddrive

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


