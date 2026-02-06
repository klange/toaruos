#include <errno.h>
#include <fcntl.h>
#include <va_list.h>
#include <stdint.h>
#include <sys/sysfunc.h>

int fcntl(int fd, int cmd, ...) {
    switch (cmd) {
        case F_GETFD:
            return 0;
        case F_SETFD:
            return 0;
        case F_DUPFD: {
            va_list ap;
            va_start(ap, cmd);
            int arg = va_arg(ap, int); /* "taken as an integer of type int" */
            va_end(ap);
            return sysfunc(TOARU_SYS_FUNC_FCNTL_DUPFD, (char*[]){(char*)(uintptr_t)fd,(char*)(uintptr_t)arg});
        }
    }
    errno = EINVAL;
    return -1;
}
