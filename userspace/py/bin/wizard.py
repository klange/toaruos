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

import yutani_mainloop

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
        self.update_shape(yutani.WindowShape.THRESHOLD_CLEAR)

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

    def focus_changed(self, msg):
        if msg.focused:
            yctx.focus_window(window.wid)

logo = "/usr/share/logo_login.png"
pages = [
    (f"<img src=\"{logo}\"></img>\n\n<h1>Welcome to ToaruOS!</h1>\n\nThis tutorial will guide you through the features of the operating system, as well as give you a feel for the UI and design principles.\n\n\nWhen you're ready to continue, press \"Next\".\n\n<link target=\"\">https://github.com/klange/toaruos</link> - <link target=\"\">http://toaruos.org</link>\n\nToaruOS is free software, released under the terms of the NCSA/University of Illinois license.",[]),
    (f"<img src=\"{logo}\"></img>\n\nIf you wish to exit the tutorial at any time, you can click the × in the upper right corner of the window.",[HintArrow(620,-5,90,True)]),
    (f"<img src=\"{logo}\"></img>\n\nAs a reminder, ToaruOS is a hobby project with few developers.\nAs such, do not expect things to work perfectly, or in some cases, at all, as the kernel and drivers are very much \"work-in-progress\".",[]),
    (f"\n<img src=\"/usr/share/icons/48/utilities-terminal.png\"></img>\nToaruOS aims to provide a Unix-like environment. You can find familiar command-line tools by opening a terminal. Application shorctus on the desktop, as well as files in the file browser, are opened with a single click. You can also find more applications in the Applications menu.",[HintHole(70.5,80.5,50),HintArrow(110,120,-135)]),
    (f"\n<img src=\"/usr/share/icons/48/folder.png\"></img>\nYou can explore the file system using the File Browser.",[HintHole(70.5,160.5,50),HintArrow(110,200,-135)]),
    (f"\n<img src=\"/usr/share/icons/48/package.png\"></img>\nMany third-party software packages have been ported to ToaruOS and are available from our website. You can use the package manager to automatically install programs like GCC, Bochs, Vim, Quake, and more.\n\nThe Package Manager will require you to authenticate. The default user is `<mono>local</mono>` with the password `<mono>local</mono>`. There is also a `root` user with password `<mono>root</mono>`.",[HintHole(70.5,240.5,50),HintArrow(110,280,-135)]),
    (f"\n\n\n\nWith ToaruOS's window manager, you can drag most windows by holding Alt, or by using the title bar. You can resize a window with Alt + Middle Click, and rotate with Alt + Right Click. To maximize a window, drag it to the top edge of the screen. To tile windows, use the Super (Windows or Command) key and the arrow keys. Holding shift or control will tile windows to quarter sizes up and down respectively.\n\nIf you are using VirtualBox, make sure the Host key does not conflict with these key bindings.",[]),
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

    def finish_resize(self, msg):
        pass # Ignore resize requests

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
        if msg.type == yutani.Message.MSG_WELCOME:
            hints.resize(msg.display_width,msg.display_height)
            window.center()
        else:
            if not yutani_mainloop.handle_event(msg):
                break
