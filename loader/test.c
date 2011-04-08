int syscall_print(const char * p1) {
	int a = 0xA5ADFACE;
	__asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (0), "b" ((int)p1));
	return a;
}

int main(int argc, char ** argv) {
    syscall_print("Hello world!");
    return 0;
}
