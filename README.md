# ToaruOS

ToaruOS is a hobby x86 operating system, built mostly from scratch.

This project has been deprecated and development has moved to [ToaruOS-NIH](https://gitlab.com/toaruos/toaru-nih), with the goal of changing the "mostly" above to "completely".

## Differences between ToaruOS and ToaruOS-NIH

This is the traditional - or "mainline" - distribution of ToaruOS. The kernel is mostly the same, though some features have been added in ToaruOS-NIH that have not been backported here. The bulk of the differences between ToaruOS "mainline" and ToaruOS-NIH are found in the userspace.

Fundamentally, mainline ToaruOS is built using Newlib. The use of a third-party, relatively complete C standard library means that mainline ToaruOS is able to support a larger collection of third-party software. ToaruOS-NIH, on the other hand, has its own C standard library. While Python 3.6 has been ported to both distributions, ToaruOS-NIH core policy of not including third-party components means that applications written in Python are not part of the "core" experience. As such, several applications which were written in Python for mainline ToaruOS are in the process of being ported to C, allowing them to remain part of the core experience of the OS. This has the added benefit of making these applications much more performant than their Python counterparts.

The use of Newlib and allowance of third-party libraries in mainline ToaruOS also means that the core UI is built on top of libraries like Cairo, libpng, and freetype, which ToaruOS-NIH uses its own graphical libraries.

## Phasing out mainline ToaruOS

As ToaruOS-NIH reaches a state of feature completeness, and libraries which had previously been ported to mainline ToaruOS are also ported to ToaruOS-NI's C libraryH, the mainline distribution will be discontinued and ToaruOS-NIH will become the only distribution of ToaruOS.

There are many obstacles to tackle on the way to making ToaruOS-NIH the core ToaruOS distribution:

- Implementing enough C standard library functionality to port key applications and libraries from the mainline distribution, such as GCC, Vim, libpng, freetype, and Cairo.
- Implementing plugin architectures so that applications can use either native libraries or these third-party libraries for graphics, font rendering, and so on.
- Development of a package manager using only in-house componenets, so that the third-party libraries and applications may be installed.

