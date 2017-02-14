#!/usr/bin/python3
"""
Login Manager

"""
import math
import os
import sys
import time

import cairo

import yutani
import text_region
import toaru_fonts
import fswait

import panel
from panel import PanelWindow, FillWidget, VolumeWidget, NetworkWidget, DateWidget, ClockWidget, RestartMenuWidget, LabelWidget

from input_box import TextInputWindow
from dialog import DialogWindow

import yutani_mainloop

class LoginWindow(yutani.Window):

    def __init__(self):
        w = yutani.yutani_ctx._ptr.contents.display_width
        h = yutani.yutani_ctx._ptr.contents.display_height

        super(LoginWindow, self).__init__(w, h, doublebuffer=True)
        self.move(0,0)
        self.set_stack(yutani.WindowStackOrder.ZORDER_BOTTOM)

        self.load_wallpaper()

    def load_wallpaper(self):
        tmp = cairo.ImageSurface.create_from_png('/usr/share/wallpapers/default')
        self.wallpaper = cairo.ImageSurface(cairo.FORMAT_ARGB32, self.width, self.height)

        x = self.width / tmp.get_width()
        y = self.height / tmp.get_height()

        nh = int(x * tmp.get_height())
        nw = int(y * tmp.get_width())

        ctx = cairo.Context(self.wallpaper)

        if (nw > self.width):
            ctx.translate((self.width - nw) / 2, 0)
            ctx.scale(y,y)
        else:
            ctx.translate(0,(self.height - nh) / 2)
            ctx.scale(x,x)

        ctx.set_source_surface(tmp,0,0)
        ctx.paint()

        buf = yutani.GraphicsBuffer(self.wallpaper.get_width(),self.wallpaper.get_height())
        tmp = buf.get_cairo_surface()
        ctx = cairo.Context(tmp)
        ctx.set_source_surface(self.wallpaper)
        ctx.paint()
        yutani.yutani_gfx_lib.blur_context_box(buf._gfx, 20)
        yutani.yutani_gfx_lib.blur_context_box(buf._gfx, 20)
        yutani.yutani_gfx_lib.blur_context_box(buf._gfx, 20)

        ctx = cairo.Context(self.wallpaper)
        ctx.set_source_surface(tmp)
        ctx.paint()
        buf.destroy()

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        # Clear
        ctx.set_source_surface(self.wallpaper)
        ctx.paint()

        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        self.resize_accept(msg.width, msg.height)
        self.reinit()

        self.load_wallpaper()
        self.draw()
        self.resize_done()
        self.flip()

    def keyboard_event(self, msg):
        pass

def maybe_animate():
    tick = int(time.time())
    if tick != panel.current_time:
        try:
            os.waitpid(-1,os.WNOHANG)
        except ChildProcessError:
            pass
        panel.current_time = tick
        panel_window.draw()

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor() # Just in case.

    panel.current_time = int(time.time())

    window = LoginWindow()
    window.draw()

    def restart_callback():
        def confirm():
            sys.exit(0)
        DialogWindow(d,"Restart","Are you sure you want to restart?",callback=confirm,icon='exit')

    restart = RestartMenuWidget()
    restart.callback = restart_callback

    widgets = [LabelWidget(os.uname().nodename), FillWidget(),VolumeWidget(),NetworkWidget(),DateWidget(),ClockWidget(),restart]
    panel_window = PanelWindow(widgets)
    panel_window.draw()

    def input_callback(input_window):
        print(input_window.tr.text)
        input_window.close()
    TextInputWindow(d,"Username?","star",text="テスト",callback=input_callback)

    fds = [yutani.yutani_ctx]
    while 1:
        # Poll for events.
        fd = fswait.fswait(fds,500)
        maybe_animate()
        if fd == 0:
            while yutani.yutani_ctx.query():
                msg = yutani.yutani_ctx.poll()
                if msg.type == yutani.Message.MSG_WELCOME:
                    panel_window.resize(msg.display_width, panel_window.height)
                    window.resize(msg.display_width, msg.display_height)
                else:
                    if not yutani_mainloop.handle_event(msg):
                        sys.exit(0)


