#include <ctype.h>

int isprint(int c) {
    return isgraph(c) || isspace(c);
}
