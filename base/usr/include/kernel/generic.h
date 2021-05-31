#pragma once

/**
 * @brief Initialize early subsystems.
 *
 * Should be called after the architecture-specific startup routine has
 * enabled all hardware required for tasking switching and memory management.
 *
 * Initializes the scheduler, shared memory subsystem, and virtual file system;
 * mounts generic device drivers, sets up the virtual /dev directory, parses
 * the architecture-provided kernel arguments, and enables scheduling by
 * launching the idle task and converting the current context to 'init'.
 */
void generic_startup(void);

/**
 * @brief Starts init.
 *
 * Should be called after all architecture-specific initialization is completed.
 * Parses the boot arguments and executes /bin/init.
 */
int generic_main(void);
