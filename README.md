# klange's OS Development Repo #
I'm writing an OS because I'm bored and want a massive project to suck up the little pieces of my time for the next few years.


## Testing it Out ##
Grab `bootdisk.img` from the git repo and load it into a virtual machine. The bootdisk will start GRUB with a single menu entry to boot the kernel off of the same diskette.

For example, you can boot the disk in `qemu`:

    qemu -fda bootdisk.img

Or you could set up a VirtualBox machine and load the floppy image.
