# Toaru Kernel Modules

とあるOS supports loadable kernel modules since 0.6. Kernel modules are relocatable ELF object files that can be included during the boot process or loaded later, and allow for kernel functionality to be extended without added complexity to the base kernel. Currently, kernel modules are used to support some device drivers (serial, ATA), virtual devices (DOS partitions, procfs, null/zero/random), file systems (ext2), and a kernel debug shell.

## Why Write a Module?

If you are implementing a hardware driver, want to supply optional extensions, or want to write additional debug shell commands, you should write a module.

## Actually Writing a Module

Modules are single C source files in the `modules/` directory. Future revisions to the build system will support modules made up of multiple C sources.

Modules have access to all exported kernel functions and symbols and use the same header include path as other parts of the kernel.

### Hello World Module

The following simple creates a simple "hello world" module. It prints "hello world" to the kernel debug log when it is loaded.

    #include <logging.h>
    #include <module.h>

    static int init(void) {
        debug_print(NOTICE, "hello world");
        return 0;
    }

    static int fini(void) {
        return 0;
    }

    MODULE_DEF(hello, init, fini);

From this example, we can see that a simple module conists of initialization function (`init`), a finish function (`fini`), and a module definition. The details of the implementation of `MODULE_DEF` are unimportant; its first argument is the name of the module, which will be used to identify it to other parts of the module system; the name of the initializer function, which will be run when loaded; and the name of the finishing function, which is run at unload. To obtain the `MODULE_DEF` macro, the `<module.h>` header must be included. We also include `<logging.h>` to get `debug_print`.

### Extended the Debug Shell

We can also use modules to extend other modules. The following example will create a module that adds a new debug shell command: `hello`.

    #include <module.h>
    #include <mod/shell.h>

    DEFINE_SHELL_FUNCTION(hello, "Print 'hello world'") {
        fs_printf(tty, "hello world\n");
        return 0;
    }

    static int init(void) {
        BIND_SHELL_FUNCTION(hello);
        return 0;
    }

    static int fini(void) {
        return 0;
    }

    MODULE_DEF(hello, init, fini);
    MODULE_DEPENDS(debugshell);

The `MODULE_DEPENDS` macro specifies a single dependency of the module - in this case, the `debugshell` module. `DEBUG_SHELL_FUNCTION` and `BIND_SHELL_FUNCTION` macros are provided by `<mod/shell.h>` and allow us to define additional shell functions and bind them. Both macros take the name of the function as their first argument, while the former also takes a description to be listed in the output of the `help` command. Debug shell functions take the following arguments: `fs_node_t * tty, int argc, char * argv[]`. They should return an `int`, with `0` indicating success.

## Other Kinds of Modules

Modules can also be used to provide virtual devices, including drivers for block devices or character devices, file systems, and more. For examples of these kinds of modules, please refer to the sources in the `modules/` directory.
