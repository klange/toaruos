int syscall_print(const char * p1) {
	int a = 0xA5ADFACE;
	__asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (1), "b" ((int)p1));
	return a;
}

int main(int argc, char ** argv) {
    for (int i = 1; i < argc; ++i) {
	syscall_print(argv[i]);
	if (i != argc - 1) {
		syscall_print(" ");
	}
    }
    syscall_print("\n");
    return 0;
}
