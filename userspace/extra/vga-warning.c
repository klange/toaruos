#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#define TERM_WIDTH  80
#define TERM_HEIGHT 25

unsigned short * vga_text = (unsigned short *)0xB8000;

void set_cell(int x, int y, char c, char fg, char bg) {
	unsigned short * cell = &vga_text[(y * TERM_WIDTH + x)];
	*cell = (c | (fg | (bg << 4)) << 8);
}

void print_message(int y, char * c) {
	int x = 0;
	while (*c) {
		set_cell(x, y, *c, 7, 0);
		++x;
		++c;
	}
}

int main(int argc, char * argv[]) {
	int x, y;
	for (y = 0; y < TERM_HEIGHT; ++y) {
		for (x = 0; x < TERM_WIDTH; ++x) {
			set_cell(x, y, ' ', 0, 0);
		}
	}

	print_message(0, "The VGA Terminal is currently deprecated.");
	print_message(1, "A new VGA terminal will be built in a future update.");

	struct timeval now;
	struct tm * timeinfo;
	char   buffer[80];

	while (1) {
		gettimeofday(&now, NULL);
		timeinfo = localtime((time_t *)&now.tv_sec);
		strftime(buffer, 80, "%H:%M:%S", timeinfo);
		print_message(4, buffer);
		sleep(1);
	}
}
