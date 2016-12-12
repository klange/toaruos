#ifndef _DLFCN_H
#define _DLFCN_H

#define RTLD_LAZY 0
#define RTLD_NOW  1


extern void * dlopen(const char *, int);
extern int dlclose(void *);
extern void * dlsym(void *, const char *);
extern char * dlerror(void);

#endif /* _DLFCN_H */
