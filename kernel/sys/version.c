/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2018 K. Lange
 */

#include <kernel/version.h>

/* Kernel name. If you change this, you're not
 * my friend any more. */
char * __kernel_name = "toaru";

/* This really shouldn't change, and if it does,
 * always ensure it still has the correct arguments
 * when used as a vsprintf() format. */
char * __kernel_version_format = "%d.%d.%d-%s";

/* Version numbers X.Y.Z */
int    __kernel_version_major = 1;
int    __kernel_version_minor = 12;
int    __kernel_version_lower = 0;

/* Kernel build suffix, which doesn't necessarily
 * mean anything, but can be used to distinguish
 * between different features included while
 * building multiple kernels. */
#ifdef KERNEL_GIT_TAG
# define STR(x) #x
# define STRSTR(x) STR(x)
# define KERNEL_VERSION_SUFFIX STRSTR(KERNEL_GIT_TAG)
#else
# define KERNEL_VERSION_SUFFIX "r"
#endif
char * __kernel_version_suffix   = KERNEL_VERSION_SUFFIX;

/* The release codename. */
char * __kernel_version_codename = "core";

/* Build architecture (should probably not be
 * here as a string, but rather some sort of
 * preprocessor macro, or pulled from a script) */
char * __kernel_arch = "i686";

/* Rebuild from clean to reset these. */
char * __kernel_build_date = __DATE__;
char * __kernel_build_time = __TIME__;

#if (defined(__GNUC__) || defined(__GNUG__)) && !(defined(__clang__) || defined(__INTEL_COMPILER))
# define COMPILER_VERSION "gcc " __VERSION__
#elif (defined(__clang__))
# define COMPILER_VERSION "clang " __clang_version__
#else
# define COMPILER_VERSION "unknown-compiler how-did-you-do-that"
#endif

char * __kernel_compiler_version = COMPILER_VERSION;
