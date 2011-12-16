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
int    __kernel_version_minor = 0;
int    __kernel_version_lower = 1;

/* Kernel build suffix, which doesn't necessarily
 * mean anything, but can be used to distinguish
 * between different features included while
 * building multiple kernels. */
char * __kernel_version_suffix   = "testing";

/* The release codename.
 *
 * History:
 *  * 0.0.X have the codename "uiharu"
 *  * 0.1.X will have the codename "saten"
 *  * 1.0.X will have the codename "mikoto"
 */
char * __kernel_version_codename = "uiharu";

/* Build architecture (should probably not be
 * here as a string, but rather some sort of
 * preprocessor macro, or pulled from a script) */
char * __kernel_arch = "i686";

/* Rebuild from clean to reset these. */
char * __kernel_build_date = __DATE__;
char * __kernel_build_time = __TIME__;

