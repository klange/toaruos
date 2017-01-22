#!/usr/bin/python3
"""
Demo application that renders into a Yutani window with Cairo.
"""
import yutani
import cairo
import math

if __name__ == '__main__':
    # Connect to the server.
    yutani.Yutani()

    # Initialize the decoration library.
    d = yutani.Decor()

    # Create a new window.
    w = yutani.Window(200+d.width(),200+d.height(),title="Python Cairo Demo")

    # Since this is Python, we can attach data to our window, such
    # as its internal width (excluding the decorations).
    w.int_width = 200
    w.int_height = 200

    # We can set window shaping...
    w.update_shape(yutani.WindowShape.THRESHOLD_HALF)

    # Move the window...
    w.move(100, 100)

    def draw_decors():
        """Render decorations for the window."""
        d.render(w)

    def draw_window():
        """Draw the window."""
        surface = w.get_cairo_surface()

        WIDTH, HEIGHT = w.width, w.height

        ctx = cairo.Context(surface)
        ctx.scale (WIDTH/1.0, HEIGHT/1.0)

        pat = cairo.LinearGradient (0.0, 0.0, 0.0, 1.0)
        pat.add_color_stop_rgba (1, 0, 0, 0, 1)
        pat.add_color_stop_rgba (0, 1, 1, 1, 1)

        ctx.rectangle (0,0,1,1)
        ctx.set_source (pat)
        ctx.fill ()

        pat = cairo.RadialGradient (0.45, 0.4, 0.1,
                        0.4,  0.4, 0.5)
        pat.add_color_stop_rgba (0, 1, 1, 1, 1)
        pat.add_color_stop_rgba (1, 0, 0, 0, 1)

        ctx.set_source (pat)
        ctx.arc (0.5, 0.5, 0.3, 0, 2 * math.pi)
        ctx.fill ()

        draw_decors()

    def finish_resize(msg):
        """Accept a resize."""

        # Tell the server we accept.
        w.resize_accept(msg.width, msg.height)

        # Reinitialize internal graphics context.
        w.reinit()

        # Calculate new internal dimensions.
        w.int_width = msg.width - d.width()
        w.int_height = msg.height - d.height()

        # Redraw the window buffer.
        draw_window()

        # Inform the server we are done.
        w.resize_done()

        # And flip.
        w.flip()

    # Do an initial draw.
    draw_window()

    # Don't forget to flip. Our single-buffered window only needs
    # the Yutani flip call, but the API will perform both if needed.
    w.flip()

    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            # All applications should attempt to exit on SESSION_END.
            w.close()
            break
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            # Print key events for debugging.
            print(f'W({msg.wid}) key {msg.event.key} {msg.event.action}')
            if msg.event.key == b'q':
                # Convention says to close windows when 'q' is pressed,
                # unless we're using keyboard input "normally".
                w.close()
                break
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            # If the focus of our window changes, redraw the borders.
            if msg.wid == w.wid:
                # This attribute is stored in the underlying struct
                # and used by the decoration library to pick which
                # version of the decorations to draw for the window.
                w.focused = msg.focused
                draw_decors()
                w.flip()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            # Resize the window.
            finish_resize(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            # Handle mouse events, first by passing them
            # to the decorator library for processing.
            if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
                # Close the window when the 'X' button is clicked.
                w.close()
                break
            else:
                # For events that didn't get handled by the decorations,
                # print a debug message with details.
                print(f'W({msg.wid}) mouse {msg.new_x},{msg.new_y}')
        msg.free()
