#pragma once

#include <kernel/hashmap.h>

extern void ksym_install(void);
extern void ksym_bind(const char * symname, void * value);
extern void * ksym_lookup(const char * symname);
