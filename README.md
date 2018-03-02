# ToaruOS-NIH

![screenshot](https://i.imgur.com/CebAZPA.png)

This is an experimental spin-off / distribution of ToaruOS which includes no third-party components.

## Building

Build a full ToaruOS, activate its toolchain, copy the kernel to `cdrom/kernel` and modules to `cdrom/mod` and you should be able to run `make`.

## Rationale

ToaruOS's kernel is entirely in-house. Its userspace, however, is built on several third-party libraries and tools, such as the Newlib C library, Freetype, Cairo, libpng, and most notably Python. While the decision to build ToaruOS on these technologies is not at all considered a mistake, the possibility remains to build a userspace entirely from scratch.

## Goals

- **Write a basic C library.**

  To support building the native ToaruOS libraries and port some basic software, a rudimentary C library is required.

- **Remove Cairo as a dependency for the compositor.**

  Cairo is a major component of the modern ToaruOS compositor, but is the only significant third-party dependency. This makes the compositor, which is a key part of what makes ToaruOS "ToaruOS", an important inclusion in this project. Very basic work has been done to allow the compositor to build and run without Cairo, but it is a naïve approach and remains very slow. Implementing Cairo's clipping and SSE-accelerated blitting operations is a must.

- **Write a vector font library.**

  Support for TrueType/OpenType TBD, but vector fonts are critical to the visual presentation of ToaruOS.

- **Support a compressed image format.**

  ToaruOS used a lot of PNGs, but maybe writing our own format would be fun.

## Roadmap

1. Enough C to port the dynamic loader.

2. Get the VGA terminal building.

3. Get the shell running.

4. De-Cairo-tize the compositor.
