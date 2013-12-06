---
layout: post
title:  "Test Post Please Ignore"
date:   2013-12-03 15:57:14
---

Apologies for the mess, I am in the middle of a major rewrite of the とあるOS homepage and still learning to use Jekyll.

This is a test post. It contains some test content.

Here's some C:

{% highlight c %}
#include <stdlib.h>
#include <assert.h>

#include "lib/window.h"
#include "lib/graphics.h"

int left, top, width, height;
window_t * wina;
gfx_context_t * ctx;

int32_t min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

void resize_callback(window_t * window) {
    width  = window->width;
    height = window->height;
    reinit_graphics_window(ctx, wina);
    draw_fill(ctx, rgb(0,0,0));
}


int main (int argc, char ** argv) {
    left   = 100;
    top    = 100;
    width  = 500;
    height = 500;

    setup_windowing();
    resize_window_callback = resize_callback;

    /* Do something with a window */
    wina = window_create(left, top, width, height);
    assert(wina);

    ctx = init_graphics_window(wina);
    draw_fill(ctx, rgb(0,0,0));

    int exit = 0;
    while (!exit) {
        w_keyboard_t * kbd = poll_keyboard_async();
        if (kbd != NULL) {
            if (kbd->key == 'q')
                exit = 1;
            free(kbd);
        }

        draw_line(ctx, rand() % width, rand() % width, rand() % height,
            rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
    }

    teardown_windowing();

    return 0;
}
{% endhighlight %}
