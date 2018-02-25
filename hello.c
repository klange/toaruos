#include <unistd.h>

#include "lib/graphics.h"

int main(int argc, char * argv[]) {
	gfx_context_t * gfx = init_graphics_fullscreen();
	draw_fill(gfx, rgb(0xFF, 0x44, 0x22));

	char * args[] = {
		"/bin/sh",
		NULL,
	};

	execvp("/bin/sh",args);

	return 0;
}
