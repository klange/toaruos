#include <system.h>

uint8_t mouse_cycle = 0;
int8_t  mouse_byte[3];
int8_t  mouse_x = 0;
int8_t  mouse_y = 0;

int32_t actual_x = 0;
int32_t actual_y = 0;

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
			mouse_x = mouse_byte[1];
			mouse_y = mouse_byte[2];
			mouse_cycle = 0;
			uint32_t previous_x = actual_x;
			uint32_t previous_y = actual_y;
			actual_x = actual_x + mouse_x;
			actual_y = actual_y + mouse_y;
			if (actual_x < 0) actual_x = 0;
			if (actual_x > 10230) actual_x = 10230;
			if (actual_y < 0) actual_y = 0;
			if (actual_y > 7670) actual_y = 7670;
			uint32_t color = 0x00FF00;
			if (mouse_byte[0] & 0x01) color = 0xFF00FF;
			bochs_draw_line(previous_x / 10, actual_x / 10, 767 - (previous_y / 10), 767 - (actual_y / 10), color);
#if 0
			kprintf("[mouse] %d %d %x\n", (uint32_t)actual_x, (uint32_t)actual_y, (uint32_t)mouse_byte[0]);
#endif
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
	return inportb(0x60);
}

void mouse_install() {
	uint8_t status;
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
	irq_install_handler(12, mouse_handler);
}
