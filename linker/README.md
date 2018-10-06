# ELF Shared Library Dynamic Linker/Loader

ToaruOS employs *shared objects* to allow for smaller on-disk binary sizes and provide runtime loading and linking.

The linker here becomes `/lib/ld.so` and is called as the interpreter for dynamically-linked binaries in the OS.

## ld.so Implementation

The linker is a minimal implementation of 32-bit x86 ELF dynamic linking. It does not (yet) employ shared file mappings for libraries or binaries. This does mean that memory usage of dynamically linked programs is generally higher than if they were statically linked, but disk space can be saved if multiple programs are using the same libraries (such as the C standard library itself). Actually sharing program code between processes is planned for the future, but requires additional functionally not yet available from the kernel.

## ld.so Debugging

You can enable debug output from the linker/loader by setting the environment variable `LD_DEBUG=1`. This will provide details on where ld.so is loading libraries, as well as reporting any unresolved symbols which it normally ignores.


