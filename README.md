# klange's OS Development Repo #
I'm writing an OS because I'm bored and want a massive project to suck up the little pieces of my time for the next few years.


## Testing it Out ##
Grab `bootdisk.img` from the git repo and load it into a virtual machine. Despite its name, this isn't a boot disk, so you'll need a bootloader like Grub to start it up:

For example:

    root (fd1)
    kernel /kernel
    boot

