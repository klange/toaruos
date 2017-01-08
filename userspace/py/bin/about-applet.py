#!/usr/bin/python3
"""
'About ToaruOS' applet
"""
import sys

import cairo

import yutani
import text_region
import toaru_fonts

class AboutAppletWindow(yutani.Window):

    base_width = 350
    base_height = 250

    text_offset = 110

    def __init__(self, decorator):
        super(AboutAppletWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title="About ToaruOS", icon="star", doublebuffer=True)
        self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))
        self.decorator = decorator
        self.logo = cairo.ImageSurface.create_from_png('/usr/share/logo_login.png')
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.base_width-30,self.base_height-self.text_offset,font=self.font)
        self.tr.set_alignment(2)
        self.tr.set_richtext("<b>ToaruOS</b>\n© 2011-2017 Kevin Lange, et al.\n\nToaruOS is free software released under the NCSA/University of Illinois license.\n\n<color 0x0000FF>http://toaruos.org\nhttps://github.com/klange/toaruos</color>")

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.set_source_surface(self.logo,(WIDTH-self.logo.get_width())/2,10)
        ctx.paint()

        self.tr.resize(WIDTH-30,HEIGHT-self.text_offset)
        self.tr.move(self.decorator.left_width() + 15,self.decorator.top_height()+self.text_offset)
        self.tr.draw(self)

        self.decorator.render(self)

    def finish_resize(self, msg):
        """Accept a resize."""
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.int_width = msg.width - self.decorator.width()
        self.int_height = msg.height - self.decorator.height()
        self.draw()
        self.resize_done()
        self.flip()

    def mouse_event(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()

    def keyboard_event(self, msg):
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = AboutAppletWindow(d)
    window.draw()
    window.flip()

    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            window.close()
            break
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            if msg.wid == window.wid:
                window.keyboard_event(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            if msg.wid == window.wid:
                window.focused = msg.focused
                window.draw()
                window.flip()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            window.finish_resize(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            if msg.wid == window.wid:
                window.mouse_event(msg)
