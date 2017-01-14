#!/usr/bin/python3
"""
Live CD wizard / tutorial
"""
import math
import os
import sys

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button

class HintHole(object):
    """Draws a hole in the hints window."""

    def __init__(self, x, y, radius):
        self.x = x
        self.y = y
        self.radius = radius

    def draw(self, ctx):
        ctx.save();
        ctx.set_operator(cairo.OPERATOR_SOURCE);
        ctx.set_source_rgba(0.0, 0.0, 0.0, 0.0);
        ctx.translate(self.x,self.y)
        ctx.arc(0, 0, self.radius, 0, 2 * math.pi);
        ctx.fill();
        ctx.restore();

_arrow = cairo.ImageSurface.create_from_png('/usr/share/wizard-arrow.png')

class HintArrow(object):

    def __init__(self, x, y, angle, wizard_relative=False):
        self.x = x
        self.y = y
        self.angle = angle
        self.wizard_relative = wizard_relative

    def draw(self, ctx):
        ctx.save()
        w,h = _arrow.get_width(), _arrow.get_height()

        if self.wizard_relative:
            ctx.translate((hints.width-window.width)/2+self.x,(hints.height-window.height)/2+self.y)
        else:
            ctx.translate(self.x,self.y)
        ctx.rotate(self.angle * math.pi / 179.0)
        ctx.translate(-w,-h/2)
        ctx.set_source_surface(_arrow,0,0)
        ctx.paint()
        ctx.restore()

class HintWindow(yutani.Window):

    def __init__(self):
        flags = yutani.WindowFlag.FLAG_NO_STEAL_FOCUS | yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE | yutani.WindowFlag.FLAG_ALT_ANIMATION
        super(HintWindow, self).__init__(yutani.yutani_ctx._ptr.contents.display_width, yutani.yutani_ctx._ptr.contents.display_height, doublebuffer=True, flags=flags)
        self.move(0,0)
        self.page = 0

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.set_source_rgba(0,0,0,100/255)
        ctx.fill()

        ctx.set_operator(cairo.OPERATOR_OVER)

        for widget in pages[self.page][1]:
            widget.draw(ctx)

        self.flip()

    def finish_resize(self, msg):
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()

logo = "/usr/share/logo_login.png"
pages = [
    (f"<img src=\"{logo}\"></img>\n\n<h1>Welcome to とあるOS!</h1>\n\nThis tutorial will guide you through the features of the operating system, as well as give you a feel for the UI and design principles.\n\n\nWhen you're ready to continue, press \"Next\".\n\n<link target=\"\">https://github.com/klange/toaruos</link> - <link target=\"\">http://toaruos.org</link>\n\nとあるOS is free software, released under the terms of the NCSA/University of Illinois license.",[]),
    (f"<img src=\"{logo}\"></img>\n\nIf you wish to exit the tutorial at any time, you can click the × in the upper right corner of the window.",[HintArrow(620,-5,90,True)]),
    (f"<img src=\"{logo}\"></img>\n\nAs a reminder, とあるOS is a hobby project with few developers.\nAs such, do not expect things to work perfectly, or in some cases, at all, as the kernel and drivers are very much \"work-in-progress\".",[]),
    (f"\n\n\n\nとあるOS aims to provide a Unix-like environment. You can find familiar command-line tools by opening a terminal. Application shorctus on the desktop, as well as files in the file browser, are opened with a single click. You can also find more applications in the Applications menu.",[HintHole(70.5,80.5,50),HintArrow(110,120,-135)]),
    (f"<img src=\"{logo}\"></img>\n\nThat's it for now!\n\nYou've finished the tutorial. More guides will be added to this tutorial in the future, but that's all for now. Press 'Exit' to close the tutorial and get started using the OS.",[]),
]

class WizardWindow(yutani.Window):

    text_offset = 110

    def __init__(self, decorator, title="Welcome to ToaruOS!", icon="star"):
        flags = yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE
        super(WizardWindow, self).__init__(640,480, title=title, icon=icon, doublebuffer=True, flags=flags)
        self.center()
        self.decorator = decorator
        hpad = 100
        self.page = 0
        self.button = Button("Next",self.button_click)
        self.hover_widget = None
        self.down_button = None
        self.tr = text_region.TextRegion(hpad+self.decorator.left_width(),10+self.decorator.top_height(),self.width-self.decorator.width()-hpad*2,self.height-self.decorator.height()-20)
        self.tr.line_height = 15
        self.tr.set_alignment(2)
        self.load_page()

    def button_click(self, button):
        self.page += 1
        if self.page >= len(pages):
            sys.exit(0)
        self.load_page()

    def load_page(self):
        self.tr.set_richtext(pages[self.page][0])
        if self.page == len(pages)-1:
            self.button.text = "Exit"
        hints.page = self.page
        hints.draw()

    def center(self):
        self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        self.tr.draw(self)
        self.button.draw(self,ctx,int((WIDTH-100)/2),380,100,38)

        self.decorator.render(self)
        self.flip()

    def mouse_event(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()

        redraw = False

        if self.down_button:
            if msg.command == yutani.MouseEvent.RAISE or msg.command == yutani.MouseEvent.CLICK:
                if not (msg.buttons & yutani.MouseButton.BUTTON_LEFT):
                    if x >= self.down_button.x and \
                        x < self.down_button.x + self.down_button.width and \
                        y >= self.down_button.y and \
                        y < self.down_button.y + self.down_button.height:
                            self.down_button.focus_enter()
                            self.down_button.callback(self.down_button)
                            self.down_button = None
                            redraw = True
                    else:
                        self.down_button.focus_leave()
                        self.down_button = None
                        redraw = True
        else:
            if x >= self.button.x and x < self.button.x + self.button.width and y >= self.button.y and y < self.button.y + self.button.height:
                if self.button != self.hover_widget:
                    if self.hover_widget:
                        self.hover_widget.leave()
                    self.button.focus_enter()
                    self.hover_widget = self.button
                    redraw = True
                if msg.command == yutani.MouseEvent.DOWN:
                    self.button.hilight = 2
                    self.down_button = self.button
                    redraw = True
            else:
                if self.hover_widget == self.button:
                    self.button.focus_leave()
                    self.hover_widget = None
                    redraw = True

        if redraw:
            self.draw()

    def keyboard_event(self, msg):
        if msg.event.action != 0x01:
            return # Ignore anything that isn't a key down.
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)
        if msg.event.key == b' ':
            self.page += 1
            if self.page >= len(pages):
                self.close()
                sys.exit(0)
            self.load_page()
            self.draw()

if __name__ == '__main__':
    yctx = yutani.Yutani()
    d = yutani.Decor()

    hints = HintWindow()
    hints.draw()

    window = WizardWindow(d)
    window.draw()

    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            window.close()
            hints.close()
            break
        elif msg.type == yutani.Message.MSG_WELCOME:
            hints.resize(msg.display_width,msg.display_height)
            window.center()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            if msg.wid == hints.wid:
                hints.finish_resize(msg)
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            if msg.wid == window.wid:
                window.keyboard_event(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            if msg.wid == hints.wid:
                yctx.focus_window(window.wid)
            if msg.wid == window.wid:
                window.focused = msg.focused
                window.draw()
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            if msg.wid == window.wid:
                window.mouse_event(msg)
