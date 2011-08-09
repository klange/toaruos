# NOTICE #
Project development will continue on **August 21st, 2011**.

# ToAruOS (とあるOS) #
This is a toy OS based on the POSIX standards. The primary goal of the project is for me to learn POSIX from the system side, understanding the design and constructon of an operating system on x86 hardware, and build a working implementation of the C standard library.

## Screenshots ##
Here's what とあるOS looks like:

* [Empty kernel shell](http://i.imgur.com/44huV.jpg)
* [Running some GNU apps](http://i.imgur.com/aKyJb.png)
* [Playing a simple RPG](http://i.imgur.com/A7csE.jpg)
* [Generating Julia fractals](http://i.imgur.com/Uy3JJ.jpg)
* [Early boot screen testing and some old serial output](http://i.imgur.com/1CMJ0.jpg)

## Testing it Out ##
Clone the git repository and run:

    make                # to build the kernel
    make run            # to run qemu with the proper arguments
    # optionally:
    sudo make install   # to install the kernel and ramdisk to your /boot directory

This will build the kernel and the ramdisk and, optionally, copy them to your boot directory so you can add them to your grub config. As of 10 Feb 2011, a bootdisk is no longer created and the git repository no longer carries a GRUB disk. Building a bootable floppy has been deprecated in favor of running with QEMU's built-in multiboot support.

My testing environment is a combination of QEMU and VirtualBox.

## Dependencies ##

### Kernel ###
To build the kernel, you will need `yasm`, `clang` (or `gcc`, change the Makefile), and GNU `ld` (which you undoubtedly have if you have `clang` or `gcc`).

### Documentation ###
To build the primary documentation, you need a complete LaTeX stack with `pdftex`, including the CJK module and Japanese fonts. To build the kernel API documentation, you will need Doxygen.

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

* Dynamic library loader
* Create a working implementation of Wayland under VESA (will be slow)
* Port Qt (under Wayland) and some Qt apps
    * Port Qt under framebuffer first? Qt has everything...
* Support audio in a way that doesn't suck like Linux's mess of libraries and mixers
* Various hardware-specific drivers (primarily for my T410):
    * Intel graphics driver, with acceleration so Wayland isn't slow
    * Realtek wireless driver, with WPA2
    * Specific drivers for the Thinkpad itself (or just acpi?)
* Custom b-tree filesystem

### Roadmap ###
Currently, I have a kernel capable of reading its multiboot parameters, which is terribly un-useful. The current, ordered, plan of attack is as follows:

* Finish James M's tutorial (second half), which covers:
    * Paging *done*
    * Heap *done, implemented with klmalloc in the void*
    * VFS *done*
    * Initial RAM Disk *works, but not perfect*
    * Multitasking *done*
    * User mode *done*
* Finish basic kernel functionality
    * Loading ELF binaries and executing them in user mode *done (static)*
    * Complete system call table
    * Get a better semblance of users and groups
* Write a file system driver for a real file system
    * Target is EXT2, but might do FAT *can read small EXT2 partitions from memory*
    * Move OS development images to some form of virtual drive (VDI or something QEMU compatible)
* Implement a VESA mode handler
    * QEMU / BOCHS VBE driver *done*
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

* [James M's kernel development tutorials](http://www.jamesmolloy.co.uk/tutorial_html/index.html) *completed*
* [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/) *completed*
* [Skelix's OS tutorial](http://en.skelix.org/skelixos/) *Author is not a native speaker of English and makes some grammatical mistakes; can be hard to follow*

## License ##

ToAruOS is under the NCSA license, which is a derivative (and fully compatible with) the BSD license. It is also forward compatible with the GPL, so you can use ToAruOS bits and pieces in GPL. The terms of the license are listed here for your convience:

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal with the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
      1. Redistributions of source code must retain the above copyright notice,
         this list of conditions and the following disclaimers.
      2. Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimers in the
         documentation and/or other materials provided with the distribution.
      3. Neither the names of the ToAruOS Kernel Development Team, Kevin Lange,
         nor the names of its contributors may be used to endorse
         or promote products derived from this Software without specific prior
         written permission.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
    CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    WITH THE SOFTWARE.

### Additional Licenses ###

ToAruOS contains additional software with the following copyright notices:

* The CPU detection code provided in `kernel/core/cpudet.c`:
  Copyright (c) 2006-2007 -  http://brynet.biz.tm - <brynet@gmail.com>
