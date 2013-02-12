/*
 *
 * Toolkit Demo and Development Application
 *
 */
#include <stdlib.h>
#include <assert.h>

#include "gui/ttk/ttk.h"

int main (int argc, char ** argv) {

	ttk_initialize();
	ttk_window_t * main_window = ttk_window_new("TTK Demo", 500, 500);

	return ttk_run(main_window);
}
