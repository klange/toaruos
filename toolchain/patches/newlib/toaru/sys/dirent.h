/* libc/sys/toaru/sys/dirent.h - Directory entry as returned by readdir */

/* Written 2000 by Werner Almesberger */
/* Hacked, and broken by Kevin Lange */


#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>
#include <bits/dirent.h>
#define _LIBC 1
#define  NOT_IN_libc 1
#include <sys/lock.h>
#undef _LIBC

#define HAVE_NO_D_NAMLEN	/* no struct dirent->d_namlen */

#define MAXNAMLEN 255		/* sizeof(struct dirent.d_name)-1 */


typedef struct {
    int dd_fd;		/* directory file */
    int dd_loc;		/* position in buffer */
    int dd_seek;
    char *dd_buf;	/* buffer */
    int dd_len;		/* buffer length */
    int dd_size;	/* amount of data in buffer */
    _LOCK_RECURSIVE_T dd_lock;
} DIR;


#define __dirfd(dir) (dir)->dd_fd

/* --- redundant --- */

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
void rewinddir(DIR *);
int closedir(DIR *);

/* internal prototype */
void _seekdir(DIR *dir, long offset);
DIR *_opendir(const char *);

#ifndef _POSIX_SOURCE
long telldir (DIR *);
void seekdir (DIR *, off_t loc);

int scandir (const char *__dir,
             struct dirent ***__namelist,
             int (*select) (const struct dirent *),
             int (*compar) (const struct dirent **, const struct dirent **));

int alphasort (const struct dirent **__a, const struct dirent **__b);
#endif /* _POSIX_SOURCE */

#endif
