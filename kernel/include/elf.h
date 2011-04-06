/*
 * ELF Binary Executable headers
 *
 * vim:noexpandtab
 * vim:tabstop=4
 */

/*
 * Different bits of our build environment
 * require different header files for definitions
 */
#ifdef _KERNEL_
#	include <types.h>
#else
#	ifdef BOOTLOADER
#		include <types.h>
#	else
#		include <stdint.h>
#	endif
#endif


