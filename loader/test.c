#include <syscall.h>

DEFN_SYSCALL1(print, 1, const char *)

int main(int argc, char ** argv) {
    syscall_print("Hello world!\n");
    return 0;
}
