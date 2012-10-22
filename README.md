# とある Compositor (Cairo version)

This is a window compositor for [とあるOS](http://github.com/klange/osdev) using Cairo.

The normal compositor that ships with とあるOS uses a built-in alpha blitting mechanism. This one uses Cairo's rendering methods to draw windows, which is faster for alpha-enabled windows than the standard compositor, but slightly slower for non-alpha windows (as they are rendered as alpha-enabled regardless).

## Dependencies

Obviously, this needs Cairo. You may need to tweak some config.h options to get Cairo to build correctly.

Eventually, Cairo will be included in the standard toolchain.

## Installation

As with other external-library applications, clone this into your `osdev` repository, build its dependencies (Cairo, Pixman, etc.), run `make` and then `make clean-disk && make` in `osdev`.

You'll also need to change `init` so it runs `compositor2` instead of `compositor`.
