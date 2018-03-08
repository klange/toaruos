#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <syscall.h>

#include "lib/trace.h"
#define TRACE_APP_NAME "font-server"

#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

static struct font_def fonts[] = {
	FONT("sans-serif",            "DejaVuSans.ttf"),
	FONT("sans-serif.bold",       "DejaVuSans-Bold.ttf"),
	FONT("sans-serif.italic",     "DejaVuSans-Oblique.ttf"),
	FONT("sans-serif.bolditalic", "DejaVuSans-BoldOblique.ttf"),
	FONT("monospace",             "DejaVuSansMono.ttf"),
	FONT("monospace.bold",        "DejaVuSansMono-Bold.ttf"),
	FONT("monospace.italic",      "DejaVuSansMono-Oblique.ttf"),
	FONT("monospace.bolditalic",  "DejaVuSansMono-BoldOblique.ttf"),
	{NULL, NULL}
};

/**
 * Preload a font into the font cache.
 *
 * TODO This should probably be moved out of the compositor,
 *      perhaps into a generic resource cache daemon. This
 *      is mostly kept this way for legacy reasons - the old
 *      compositor did it, but it was also using some of the
 *      fonts for internal rendering. We don't draw any text.
 */
static char * precache_shmfont(char * ident, char * name) {
	FILE * f = fopen(name, "r");
	if (!f) return NULL;
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);

	size_t shm_size = s;
	char * font = (char *)syscall_shm_obtain(ident, &shm_size);
	assert((shm_size >= s) && "shm_obtain returned too little memory to load a font into!");

	fread(font, s, 1, f);

	fclose(f);
	return font;
}

/**
 * Load all of the fonts into the cache.
 */
static void load_fonts(char * server) {
	int i = 0;
	while (fonts[i].identifier) {
		char tmp[100];
		sprintf(tmp, "sys.%s.fonts.%s", server, fonts[i].identifier);
		TRACE("Loading font %s -> %s", fonts[i].path, tmp);
		if (!precache_shmfont(tmp, fonts[i].path)) {
			TRACE("  ... failed.");
		}
		++i;
	}
}

int main(int argc, char * argv[]) {

	load_fonts(getenv("DISPLAY"));

	while (1) {
		usleep(100000);
	}

	return 0;
}
