#include <stdio.h>

int main(int argc, char * argv[]) {
    printf("\033[1003z");
    fflush(stdout);
    int width, height;
    scanf("%d,%d", &width, &height);
    printf("Terminal is %dx%d\n", width, height);
}
