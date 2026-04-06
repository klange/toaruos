#include <unistd.h>
#include <stdio.h>
#include <syscall_nums.h>

int main(int argc, char * argv[]){
	long ret = 0;
#ifdef __x86_64__
	__asm__ __volatile__("syscall" : "=a"(ret) : "a"(SYS_WRITE), "D"(STDOUT_FILENO), "S"("Hello, world.\n"), "d"((long)14) : "rcx", "r11", "memory");
	__asm__ __volatile__("syscall" : "=a"(ret) : "a"(SYS_WRITE), "D"(STDOUT_FILENO), "S"("Hello, world.\n"), "d"((long)14) : "rcx", "r11", "memory");
	__asm__ __volatile__("syscall" : "=a"(ret) : "a"(SYS_WRITE), "D"(STDOUT_FILENO), "S"("Hello, world.\n"), "d"((long)14) : "rcx", "r11", "memory");
#endif

	return ret;
}
