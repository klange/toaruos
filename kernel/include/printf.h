#ifndef _PRINTF_H
#define _PRINTF_H

#include <fs.h>

extern size_t vasprintf(char * buf, const char *fmt, va_list args);
extern int    sprintf(char *buf, const char *fmt, ...);
extern int    fprintf(fs_node_t * device, char *fmt, ...);

#endif /* _PRINTF_H */
