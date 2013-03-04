/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#include <version.h>

/* Kernel name. If you change this, you're not
 * my friend any more. */
char * __kernel_name = "toaru";

/* This really shouldn't change, and if it does,
 * always ensure it still has the correct arguments
 * when used as a vsprintf() format. */
char * __kernel_version_format = "%d.%d.%d-%s";

/* Version numbers X.Y.Z */
int    __kernel_version_major = 0;
int    __kernel_version_minor = 5;
int    __kernel_version_lower = 0;

/* Kernel build suffix, which doesn't necessarily
 * mean anything, but can be used to distinguish
 * between different features included while
 * building multiple kernels. */
char * __kernel_version_suffix   = "dev";

/* The release codename.
 *
 * History:
 *  * 0.0.X+ are part of the "uiharu" family
 *  * 0.5.X+ branches make up the "neopolitan flavors" family.
 *    0.5.0  is strawberry
 */
char * __kernel_version_codename = "strawberry";

/* Build architecture (should probably not be
 * here as a string, but rather some sort of
 * preprocessor macro, or pulled from a script) */
char * __kernel_arch = "i686";

/* Rebuild from clean to reset these. */
char * __kernel_build_date = __DATE__;
char * __kernel_build_time = __TIME__;

