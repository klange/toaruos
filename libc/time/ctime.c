#include <time.h>
#include <sys/time.h>

/*
 * TODO: Also supposed to set tz values...
 */
char * ctime(const time_t * timep) {
    return asctime(localtime(timep));
}
