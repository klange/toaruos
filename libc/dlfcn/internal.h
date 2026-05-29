#pragma once
#include <stdbool.h>

/* ld.so internal stuff */
int __libc_load_from_file(int fd, const char * name, int argc, char *argv[]);
int __libc_start(int argc, char *argv[], char *envp[]);
int __ld_so_main(int argc, char * argv[]);
extern bool __is_ldd;
extern char *__ld_preload;

/* stubs wanted by gcc */
int __cxa_atexit(void (*fn)(void *), void * arg, void *d);
void __register_frame_info(void);
void __deregister_frame_info(void);
void _ITM_registerTMCloneTable(void);
void _ITM_deregisterTMCloneTable(void);
void __cxa_finalize(void);
