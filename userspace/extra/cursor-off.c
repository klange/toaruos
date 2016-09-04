static void outb(unsigned char _data, unsigned short _port) {
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

int main(int argc, char * argv[]) {
	/* This should remove the hardware cursor. */
	outb(14, 0x3D4);
	outb(0xFF, 0x3D5);
	outb(15, 0x3D4);
	outb(0xFF, 0x3D5);
}
