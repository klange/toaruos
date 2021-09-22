int main(int argc, char * argv[]) {
	*(volatile int*)0x12345 = 42;
	return 0;
}
