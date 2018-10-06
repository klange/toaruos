#include <fcntl.h>

int fcntl(int fd, int cmd, ...) {
    switch (cmd) {
        case F_GETFD:
            return 0;
        case F_SETFD:
            return 0;
    }
    return -1;
}
