#include <stdlib.h>
#include <locale.h>

char * setlocale(int category, const char *locale) {
    return "en_US";
}

int __mb_cur_max(void) {
    return 1; /* TODO */
}
