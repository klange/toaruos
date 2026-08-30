#pragma once

extern const char * __kernel_name;

#define KERNEL_VERSION_FORMAT    "%d.%d.%d%s-%s"
#define KERNEL_VERSION_ELEMENTS  __kernel_version_major, __kernel_version_minor, __kernel_version_lower, __kernel_version_tag, __kernel_version_suffix

extern int    __kernel_version_major;
extern int    __kernel_version_minor;
extern int    __kernel_version_lower;

extern const char * __kernel_version_suffix;
extern const char * __kernel_version_codename;
extern const char * __kernel_version_tag;

extern const char * __kernel_arch;

extern const char * __kernel_build_date;
extern const char * __kernel_build_time;

extern const char * __kernel_compiler_version;

