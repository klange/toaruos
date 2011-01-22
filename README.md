# ToAruOS (とあるOS) #
This is a toy OS based on the POSIX standards. The primarily goal of the project is for me to learn POSIX from the system side, understanding the design and constructon of an operating system on x86 hardware, and build a working implementation of the C standard library.

## Testing it Out ##
Clone the git repository and run `make` and `sudo make install` (yes, the `sudo` is necessary because of how I am building my floppy image). This will build a working `bootdisk.img` that you can load with an emulator. If you have QEMU installed, you can then run `make run` to start the emulator. You should see a GRUB menu with one entry which should boot into the kernel.

My testing environment is a combination of QEMU and VirtualBox.

## Goals and Roadmap ##
Overall, the goal of this project is to write a relatively POSIX-compatible OS from the ground up. With a focus on generic hardware functionality and universal specifications like VESA, I hope to eventually get something fairly complete in terms of what an OS should be. At some times, I may focus on an actual piece of complex hardware (I am looking to write a basic driver for Intel graphics cards based on the X driver and the Mesa components), but in general, I will stick to generic interfaces.

### Basic Goals ###
* Create a working modular monolithic kernel capable of executing arbitrary ELF binaries
* Write, from scratch, a C standard library using past experience in writing standard library functions
* Support POSIX threads
* Implement an existing file system, specifically EXT2
* Be able to manipulate VESA modes to run at an optimal resolution for graphics
* Handle basic networking on a virtual Ethernet device (DHCP, TCP, etc.)

### Loftier Goals ###
Some things are far easier said than done, but I like to say them anyway. The time span for these depends greatly on how quickly the basic goals are completed and can range anywhere from a few months to years from now.

* Create a working implementation of Wayland under VESA (will be slow)
* Port Qt (under Wayland) and some Qt apps
* Support audio in a way that doesn't suck like Linux's mess of libraries and mixers
* Various hardware-specific drivers (primarily for my T410):
    * Intel graphics driver, with acceleration so Wayland isn't slow
    * Realtek wireless driver, with WPA2
    * Specific drivers for the Thinkpad itself (or just acpi?)

### Roadmap ###
Currently, I have a kernel capable of reading its multiboot parameters, which is terribly un-useful. The current, ordered, plan of attack is as follows:

* Finish James M's tutorial (second half), which covers:
    * Paging *done*
    * Heap
    * VFS
    * Initial RAM Disk (except I'll probably use my own format for the directory structure)
    * Multitasking
    * User mode
* Finish basic kernel functionality
    * Loading ELF binaries and executing them in user mode
    * Complete system call table
    * Get a better semblance of users and groups
* Write a file system driver for a real file system
    * Target is EXT2, but might do FAT
    * Move OS development images to some form of virtual drive (VDI or something QEMU compatible)
* Implement a VESA mode handler
    * Requires a Virtual 8086 monitor
    * Need to be able to use graphics modes and still have output, so write a framebuffer terminal
* Complete libc
    * Enough to run basic unix tools...
* Port some basic UNIX tools
    * a shell (bash and zsh, because I like bash, but the office uses zsh)
    * ls, mv, rm, etc.
    * here's a real test: perl
* Implement networking
    * IPv4
    * Ethernet driver for QEMU or VirtualBox

*Anything beyond this point is part of the 'Loftier Goals' section*

* Wayland compositor
    * based on specifications for a Wayland environment
    * Port some of the Wayland sample applications
    * Write my own!
* Port Qt
    * Specifically, for Wayland
    * Qt is huge and has its own standard library, might need more extensive libc
    * Need to support C++-built stuff, so will probably need a C++ stdlib.
* Audio drivers
    * Maybe before Qt?
    * Should support software mixing at least, hardware under a virtual machine, maybe my Intel hw

## References ##
I'll be more detailed here eventually, but for the most part, I have been using:

* [James M's kernel development tutorials](http://www.jamesmolloy.co.uk/tutorial_html/index.html)
* [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/) *completed*
* [Skelix's OS tutorial](http://en.skelix.org/skelixos/)

