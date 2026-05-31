#pragma once

extern void __atexit_run(void);
extern void __libc_take_malloc_lock(void);
extern void __libc_release_malloc_lock(void);
extern void __libc_init(void);

extern void __libc_start_main(int argc, char * argv[], char ** envp, int (*main)(int,char**));

#define _hidden __attribute__((visibility("hidden")))
