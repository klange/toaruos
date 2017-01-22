#!/usr/bin/python3
"""
Demo of rudimentary (LTR, simple fonts only) text layout.
"""
import sys

import yutani
import toaru_fonts
import text_region

ipsum = """    Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque orci mauris, vulputate at purus id, hendrerit volutpat est. Mauris vel nunc nulla. Aliquam justo massa, lacinia ac consequat sed, maximus mollis dolor. Nulla et risus sollicitudin, fermentum diam sed, efficitur dui. これは日本語です。 Ut eu mauris vel lorem ornare finibus vitae finibus massa. Süspendísse dàpibús ipsum eu accumsañ euismod. Proin pellentesque tellus vehicula convallis euismod. Donec aliquam pretium gravida. Quisque laoreet ut dolor non tincidunt. Mauris ultrices magna at ligula dictum accumsan. Nunc eleifend sollicitudin purus. In arcu orci, interdum sed ultricies id, tempor vel massa. Morbi bibendum nunc sed felis gravida tristique. Praesent vestibulum sem id mi pretium posuere.

    Proin maximus bibendum porta. Vestibulum cursus et augue at fermentum. In tincidunt, risus a placerat sollicitudin, nibh nisl tincidunt quam, laoreet tincidunt massa erat id turpis. Sed nulla augue, aliquam sit amet velit id, interdum rutrum lectus. Morbi metus tellus, commodo vitae facilisis sed, porttitor sed sem. Integer dignissim vel sem vitae euismod. Nullam et nunc sit amet felis iaculis mollis. Donec ac metus ex. Sed suscipit felis arcu, et tincidunt magna fringilla eu. Sed hendrerit, odio at condimentum tempus, metus felis volutpat metus, sed gravida lorem mi sit amet turpis. Etiam porta sodales vehicula. Integer iaculis eros sed interdum convallis. Sed rhoncus orci ligula. Proin euismod lorem ut nisl vulputate, a hendrerit felis rhoncus. Ut efficitur placerat ipsum, eu consequat nisl fermentum eget. Pellentesque id volutpat arcu, ac molestie leo."""

rich_demo="""<b>This</b> is a demon<i>stration</i> of rich text in <mono>ToaruOS</mono>. このデモは<color 0x0000FF>日本語</color>も出来ます。 At the moment, <i>this</i> <color 0xFF0000>demo</color> only supports a few markup options in an HTML-esque syntax.\nWe <b>can <i>combine <color 0x00BB00>multiple</color> different</i> options</b>."""


if __name__ == '__main__':
    # Connect to the server.
    yutani.Yutani()

    # Initialize the decoration library.
    d = yutani.Decor()

    # Create a new window.
    w = yutani.Window(600+d.width(),150+d.height(),title="Text Layout Demo",doublebuffer=True)

    # We can set window shaping...
    w.update_shape(yutani.WindowShape.THRESHOLD_HALF)

    pad = 4

    tr = text_region.TextRegion(d.left_width() + pad,d.top_height() + pad, w.width - d.width() - pad * 2, w.height - d.height() - pad * 2)
    tr.set_line_height(20)
    bold = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD,13)
    blue = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,13,0xFF0000FF)
    #with open('/usr/share/licenses') as f:
    #    tr.set_text(f.read())
    #    for unit in tr.text_units:
    #        if unit.string.startswith("http://") or unit.string.startswith("https://"):
    #            unit.set_font(blue)
    #        if unit.string == "Software":
    #            unit.set_font(bold)
    #    tr.reflow()
    #tr.set_text(ipsum)
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            tr.set_richtext(f.read())
    else:
        tr.set_richtext(rich_demo)

    # Move the window...
    w.move(100, 100)

    def draw_decors():
        """Render decorations for the window."""
        d.render(w)

    def draw_window():
        """Draw the window."""
        def rgb(r,g,b):
            return yutani.yutani_gfx_lib.rgb(r,g,b)
        w.fill(rgb(214,214,214))

        tr.draw(w)

        draw_decors()

    def finish_resize(msg):
        """Accept a resize."""

        if msg.width < 100 or msg.height < 100:
            w.resize_offer(max(msg.width,100),max(msg.height,100))
            return

        # Tell the server we accept.
        w.resize_accept(msg.width, msg.height)

        # Reinitialize internal graphics context.
        w.reinit()

        tr.resize(msg.width - d.width() - pad * 2, msg.height - d.height() - pad * 2)

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
            if msg.event.action != 0x01:
                continue
            if msg.event.key == b'c':
                tr.set_alignment(2)
                draw_window()
                w.flip()
            elif msg.event.key == b'l':
                tr.set_alignment(0)
                draw_window()
                w.flip()
            elif msg.event.key == b'r':
                tr.set_alignment(1)
                draw_window()
                w.flip()
            elif msg.event.key == b'm':
                tr.set_valignment(2)
                draw_window()
                w.flip()
            elif msg.event.key == b'b':
                tr.set_valignment(1)
                draw_window()
                w.flip()
            elif msg.event.key == b't':
                tr.set_valignment(0)
                draw_window()
                w.flip()
            elif msg.event.keycode == 2015:
                tr.scroll = 0
                draw_window()
                w.flip()
            elif msg.event.keycode == 2016:
                tr.scroll = len(tr.lines)-tr.visible_lines()
                draw_window()
                w.flip()
            elif msg.event.key == b' ' or msg.event.keycode == 2013:
                tr.scroll += int(tr.visible_lines() / 2)
                if tr.scroll > len(tr.lines)-tr.visible_lines():
                    tr.scroll = len(tr.lines)-tr.visible_lines()
                draw_window()
                w.flip()
            elif msg.event.keycode == 2014:
                tr.scroll -= int(tr.visible_lines() / 2)
                if tr.scroll < 0:
                    tr.scroll = 0
                draw_window()
                w.flip()
            elif msg.event.key == b'o':
                tr.set_one_line()
                tr.set_ellipsis()
                draw_window()
                w.flip()
            elif msg.event.key == b'i':
                tr.set_one_line(False)
                tr.set_ellipsis("")
                draw_window()
                w.flip()
            elif msg.event.key == b'q':
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
                e = tr.click(msg.new_x,msg.new_y)
                if e and msg.command == 0:
                    new_font = toaru_fonts.Font(e.font.font_number,e.font.font_size,0xFFFF0000)
                    e.set_font(new_font)
                    draw_window()
                    w.flip()
        msg.free()

