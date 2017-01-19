#!/usr/bin/python3
"""
'About ToaruOS' applet
"""
import os
import sys

import cairo

import yutani
import text_region
import toaru_fonts

import yutani_mainloop


def version():
    """Get a release from uname without a git short sha."""
    release = os.uname().release
    if '-' in release:
        return release[:release.index('-')]
    return release

_default_text = f"<b>ToaruOS {version()}</b>\n© 2011-2017 Kevin Lange, et al.\n\nToaruOS is free software released under the NCSA/University of Illinois license.\n\n<color 0x0000FF>http://toaruos.org\nhttps://github.com/klange/toaruos</color>"

class AboutAppletWindow(yutani.Window):

    base_width = 350
    base_height = 250

    text_offset = 110

    def __init__(self, decorator, title="About ToaruOS", icon="star", logo="/usr/share/logo_login.png", text=_default_text):
        super(AboutAppletWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=title, icon=icon, doublebuffer=True)
        self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))
        self.decorator = decorator
        self.logo = cairo.ImageSurface.create_from_png(logo)
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.base_width-30,self.base_height-self.text_offset,font=self.font)
        self.tr.set_alignment(2)
        self.tr.set_richtext(text)

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.set_source_surface(self.logo,int((WIDTH-self.logo.get_width())/2),10+int((84-self.logo.get_height())/2))
        ctx.paint()

        self.tr.resize(WIDTH-30,HEIGHT-self.text_offset)
        self.tr.move(self.decorator.left_width() + 15,self.decorator.top_height()+self.text_offset)
        self.tr.draw(self)

        self.decorator.render(self)
        self.flip()

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

    if len(sys.argv) > 4:
        window = AboutAppletWindow(d,sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4])
    else:
        window = AboutAppletWindow(d)
    window.draw()

    yutani_mainloop.mainloop()
