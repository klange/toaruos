# Kernel Modules

The Toaru kernel supports loadable modules which provide most of the device driver support.

A simple module requires a load and unload method, which are exposed along with a module
name through the `MODULE_DEF` macro available from `<kernel/module.h>`.

```c
#include <kernel/module.h>

static int load(void) {
	/* Run on module installation */
	return 0;
}

static int unload(void) {
	/* Clean up for removal */
	return 0;
}

MODULE_DEF(example_mod, load, unload);
```

## Module Dependencies

If your module depends on another module being loaded, list each dependency using the `MODULE_DEPENDS` macro:

```c
MODULE_DEF(extension_mod, load, unload);
MODULE_DEPENDS(example_mod);
```

Currently, dependencies are tested at load time, but the kernel will not load dependencies for you.

Dependency lists can be parsed by external tools to ensure modules are properly linked.

## Kernel Functions

All non-static kernel functions are available for use in modules.
For example, the logging functions may be used:

```c
#include <kernel/logging.h>
#include <kernel/module.h>

static int load(void) {
	debug_print(WARNING, "Hello, world.");
	return 0;
}

static int unload(void) {
	return 0;
}

MODULE_DEF(printing_mod, load, unload);
```

## Background Tasks

Device drivers, such as those for network devices, may want to create a background process to manage tasks.
This can be done through the `create_kernel_tasklet` interface at device startup. 

```c
#include <kernel/process.h>
#include <kernel/module.h>

static void tasklet_run(void * data, char * name) {
	/* Perform tasklet activities */
	while (1) {
		do_thing();
	}
}

static int load(void) {
	create_kernel_tasklet(tasklet_run, "[demo-tasklet]", NULL);
	return 0;
}

static int unload(void) {
	/* Maybe clean up your tasklet here. */
	return 0;
}

MODULE_DEF(tasklet_mod, load, unload);
```

## Caveats

- Currently, unloading modules is not supported.
- Modules which are expected to be loaded at runtime should be very careful of memory allocations they make, as they happen in a context which may not be shared with other processes. If you wish to make use of memory mappings, ensure that you are creating a new kernel tasklet to perform work. Attempting to access mapped memory from an interrupt handler or a device driver may not be possible if it was mapped at module installation.

