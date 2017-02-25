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

from input_box import InputBox
from dialog import DialogWindow

import yutani_mainloop

def rounded_rectangle(ctx,x,y,w,h,r):
    degrees = math.pi / 180
    ctx.new_sub_path()

    ctx.arc(x + w - r, y + r, r, -90 * degrees, 0 * degrees)
    ctx.arc(x + w - r, y + h - r, r, 0 * degrees, 90 * degrees)
    ctx.arc(x + r, y + h - r, r, 90 * degrees, 180 * degrees)
    ctx.arc(x + r, y + r, r, 180 * degrees, 270 * degrees)
    ctx.close_path()

class LoginWindow(yutani.Window):

    def __init__(self):
        w = yutani.yutani_ctx._ptr.contents.display_width
        h = yutani.yutani_ctx._ptr.contents.display_height

        super(LoginWindow, self).__init__(w, h, doublebuffer=True)
        self.move(0,0)
        self.set_stack(yutani.WindowStackOrder.ZORDER_BOTTOM)

        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 11, 0xFFFFFFFF)
        self.font.set_shadow((0xFF000000, 2, 1, 1, 3.0))
        self.tr = text_region.TextRegion(0,0,200,30,font=self.font)
        self.tr.set_text(f"ToaruOS {os.uname().release}")

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

        # Paint blurred wallpaper
        ctx.set_source_surface(self.wallpaper)
        ctx.paint()

        self.tr.move(10,self.height-24)
        self.tr.draw(self)

        self.flip()

    def focus_changed(self, msg):
        if msg.focused:
            yutani.yutani_ctx.focus_window(prompts.wid)

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

class InputWindow(yutani.Window):

    def __init__(self):
        _w = yutani.yutani_ctx._ptr.contents.display_width
        _h = yutani.yutani_ctx._ptr.contents.display_height

        self.logo = cairo.ImageSurface.create_from_png('/usr/share/logo_login.png')

        super(InputWindow, self).__init__(272, self.logo.get_height() + 110 + 50, doublebuffer=True)
        self.update_position(_w,_h)

        self.username = InputBox(placeholder="Username",width=180)
        self.password = InputBox(password=True,placeholder="Password",width=180)
        self.focused_widget = None

        self.username.tab_handler = self.focus_password
        self.password.tab_handler = self.focus_username

        self.username.submit = self.password_or_go
        self.password.submit = self.go

        self.error_font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 11, 0xFFFF0000)
        self.error_font.set_shadow((0xFF000000, 2, 0, 0, 3.0))
        self.error_tr = text_region.TextRegion(0,0,self.width,20,font=self.error_font)
        self.error_tr.set_alignment(2)

        self.error = None

    def focus_changed(self, msg):
        if not msg.focused:
            self.username.focus_leave()
            self.password.focus_leave()

    def update_position(self, w, h):
        self.move(int((w - self.width)/2),int((h - self.height)/2))

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        # Clear
        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_source_rgba(0,0,0,0)
        ctx.fill()

        ctx.set_operator(cairo.OPERATOR_OVER)

        ctx.set_source_surface(self.logo, (self.width - self.logo.get_width())/2, 0)
        ctx.paint()

        base = self.height - 110

        rounded_rectangle(ctx, 0, base, self.width, 110, 4)
        ctx.set_source_rgba(0,0,0,0.5)
        ctx.fill()

        if self.error:
            self.error_tr.move(0,base+8)
            self.error_tr.set_text(self.error)
            self.error_tr.draw(self)

        ctx.save()
        ctx.translate(46,base + 30)
        self.username.draw(self,ctx)
        ctx.restore()

        ctx.save()
        ctx.translate(46,base + 60)
        self.password.draw(self,ctx)
        ctx.restore()

        self.flip()

    def keyboard_event(self, msg):
        if self.focused_widget:
            self.focused_widget.keyboard_event(msg)
        else:
            self.focus_username()
            self.focused_widget.keyboard_event(msg)

    def focus_password(self):
        self.username.focus_leave()
        self.password.focus_enter()
        self.focused_widget = self.password
        self.draw()

    def focus_username(self):
        self.password.focus_leave()
        self.username.focus_enter()
        self.focused_widget = self.username
        self.draw()

    def password_or_go(self):
        if self.password.text:
            self.go()
        else:
            self.focus_password()

    def go(self):
        print(f"Your username is {self.username.text} and your password is {self.password.text}")

    def mouse_event(self, msg):
        if self.username.mouse_event(msg):
            self.focus_username()
        elif self.password.mouse_event(msg):
            self.focus_password()
        elif msg.command == yutani.MouseEvent.DOWN:
            self.password.focus_leave()
            self.username.focus_leave()
            self.focused_widget = None

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

    prompts = InputWindow()
    prompts.draw()

    def restart_callback():
        def confirm():
            sys.exit(0)
        DialogWindow(d,"Restart","Are you sure you want to restart?",callback=confirm,icon='exit')

    restart = RestartMenuWidget()
    restart.callback = restart_callback

    widgets = [LabelWidget(os.uname().nodename), FillWidget(),VolumeWidget(),NetworkWidget(),DateWidget(),ClockWidget(),restart]
    panel_window = PanelWindow(widgets)
    panel_window.draw()

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
                    prompts.update_position(msg.display_width, msg.display_height)
                else:
                    if not yutani_mainloop.handle_event(msg):
                        sys.exit(0)


