# Shared Object C library

This worked to build a libc.so:

1. Remove `init.o` and `fini.o` from `libc.a`
2. `i686-pc-toaru-gcc -shared -o libc.so -Wl,--whole-archive libc.a -Wl,--no-whole-archive`
3. Remember to not attempt to build userspace like this!

To build a shared object:

    i686-pc-toaru-gcc -shared -o libfoo.so foo.c

To build against a shared object in the local directory:

    i686-pc-toaru-gcc -L`pwd` -o test test.c -lfoo
