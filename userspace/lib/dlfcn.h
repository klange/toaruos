#ifndef _DLFCN_H
#define _DLFCN_H



extern void * dlopen(const char *, int);
extern int dlclose(void *);
extern void * dlsym(void *, const char *);
extern char * dlerror(void);

#endif /* _DLFCN_H */
