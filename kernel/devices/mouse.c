/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Mouse driver
 */
#include <system.h>
#include <logging.h>
#include <pipe.h>
#include <mouse.h>

uint8_t mouse_cycle = 0;
int8_t  mouse_byte[3];

#define PACKETS_IN_PIPE 64
#define DISCARD_POINT 32

fs_node_t * mouse_pipe;

void mouse_handler(struct regs *r) {
	switch (mouse_cycle) {
		case 0:
			mouse_byte[0] = inportb(0x60);
			++mouse_cycle;
			break;
		case 1:
			mouse_byte[1] = inportb(0x60);
			++mouse_cycle;
			break;
		case 2:
			mouse_byte[2] = inportb(0x60);
			mouse_device_packet_t packet;
			packet.magic = MOUSE_MAGIC;
			packet.x_difference = mouse_byte[1];
			packet.y_difference = mouse_byte[2];
			packet.buttons = 0;
			if (mouse_byte[0] & 0x01) {
				packet.buttons |= LEFT_CLICK;
			}
			if (mouse_byte[0] & 0x02) {
				packet.buttons |= RIGHT_CLICK;
			}
			mouse_cycle = 0;

			mouse_device_packet_t bitbucket;
			while (pipe_size(mouse_pipe) > (DISCARD_POINT * sizeof(packet))) {
				read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
			}
			write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
			break;
	}
}

void mouse_wait(uint8_t a_type) {
	uint32_t timeout = 100000;
	if (!a_type) {
		while (--timeout) {
			if ((inportb(0x64) & 0x01) == 1) {
				return;
			}
		}
		return;
	} else {
		while (--timeout) {
			if (!((inportb(0x64) & 0x02))) {
				return;
			}
		}
		return;
	}
}

void mouse_write(uint8_t write) {
	mouse_wait(1);
	outportb(0x64, 0xD4);
	mouse_wait(1);
	outportb(0x60, write);
}

uint8_t mouse_read() {
	mouse_wait(0);
	char t = inportb(0x60);
	return t;
}

void mouse_install() {
	LOG(INFO, "Initializing mouse cursor driver");
	uint8_t status;
	IRQ_OFF;
	mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
	mouse_wait(1);
	outportb(0x64,0xA8);
	mouse_wait(1);
	outportb(0x64,0x20);
	mouse_wait(0);
	status = inportb(0x60) | 2;
	mouse_wait(1);
	outportb(0x64, 0x60);
	mouse_wait(1);
	outportb(0x60, status);
	mouse_write(0xF6);
	mouse_read();
	mouse_write(0xF4);
	mouse_read();
	IRQ_RES;
	irq_install_handler(12, mouse_handler);
}
