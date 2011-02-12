/**
 * Serial Port Driver
 */
#include <system.h>

#define SERIAL_PORT_A 0x3F8
#define SERIAL_PORT_B 0x2F8
#define SERIAL_PORT_C 0x3E8
#define SERIAL_PORT_D 0x2E8

void
serial_install() {
	/* We will initialize the first serial port */
	outportb(SERIAL_PORT_A + 1, 0x00);
	outportb(SERIAL_PORT_A + 3, 0x80);
	outportb(SERIAL_PORT_A + 0, 0x03);
	outportb(SERIAL_PORT_A + 1, 0x00);
	outportb(SERIAL_PORT_A + 3, 0x03);
	outportb(SERIAL_PORT_A + 2, 0xC7);
	outportb(SERIAL_PORT_A + 4, 0x0B);
}

int
serial_rcvd() {
	return inportb(SERIAL_PORT_A + 5) & 1;
}

char
serial_recv() {
	while (serial_rcvd() == 0);
	return inportb(SERIAL_PORT_A);
}

int
serial_transmit_empty() {
	return inportb(SERIAL_PORT_A + 5) & 0x20;
}

void
serial_send(char out) {
	while (serial_transmit_empty() == 0);
	outportb(SERIAL_PORT_A, out);
}
