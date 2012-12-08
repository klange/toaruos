/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Serial Port Driver
 */
#include <system.h>
#include <logging.h>

#if 0
void serial_handler_a(struct regs *r) {
	char serial = serial_recv(SERIAL_PORT_A);
	irq_ack(SERIAL_IRQ);
	if (serial == 13) serial = '\n';
	kprintf("%c", serial);
	serial_send(SERIAL_PORT_B, serial);
}
void serial_handler_b(struct regs *r) {
	char serial = serial_recv(SERIAL_PORT_B);
	irq_ack(SERIAL_IRQ - 1);
	serial_send(SERIAL_PORT_A, serial);
}
#endif

void serial_enable(int device) {
	outportb(device + 1, 0x00);
	outportb(device + 3, 0x80); /* Enable divisor mode */
	outportb(device + 0, 0x03); /* Div Low:  03 Set the port to 38400 bps */
	outportb(device + 1, 0x00); /* Div High: 00 */
	outportb(device + 3, 0x03);
	outportb(device + 2, 0xC7);
	outportb(device + 4, 0x0B);
}

void
serial_install() {
	debug_print(NOTICE, "Installing serial communication driver");

	serial_enable(SERIAL_PORT_A);
	serial_enable(SERIAL_PORT_B);

#if 0
	irq_install_handler(SERIAL_IRQ, serial_handler_a); /* Install the serial input handler */
	irq_install_handler(SERIAL_IRQ - 1, serial_handler_b); /* Install the serial input handler */
	outportb(SERIAL_PORT_A + 1, 0x01);      /* Enable interrupts on receive */
	outportb(SERIAL_PORT_B + 1, 0x01);      /* Enable interrupts on receive */
#endif
}

int serial_rcvd(int device) {
	return inportb(device + 5) & 1;
}

char serial_recv(int device) {
	while (serial_rcvd(device) == 0) ;
	return inportb(device);
}

char serial_recv_async(int device) {
	return inportb(device);
}

int serial_transmit_empty(int device) {
	return inportb(device + 5) & 0x20;
}

void serial_send(int device, char out) {
	while (serial_transmit_empty(device) == 0);
	outportb(device, out);
}

void serial_string(int device, char * out) {
	for (uint32_t i = 0; i < strlen(out); ++i) {
		serial_send(device, out[i]);
	}
}
