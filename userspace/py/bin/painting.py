#!/usr/bin/python3
"""
Painting
"""
import os
import math
import stat
import sys
import subprocess

import cairo

import yutani
import text_region
import toaru_fonts
import toaru_package

from color_picker import ColorPickerWindow

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from icon_cache import get_icon

version = "0.1.0"
_description = f"<b>ToaruPaint {version}</b>\n© 2017 Kevin Lange\n\nDraw stuff, maybe.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

class PaintingWindow(yutani.Window):

    base_width = 600
    base_height = 600

    def __init__(self, decorator):
        super(PaintingWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title="ToaruPaint", icon="applications-painting", doublebuffer=True)
        self.move(100,100)
        self.x = 100
        self.y = 100
        self.decorator = decorator
        self.picker = None
        self.last_color = (0,0,0)

        def exit_app(action):
            menus = [x for x in self.menus.values()]
            for x in menus:
                x.definitely_close()
            self.close()
            sys.exit(0)

        def about_window(action):
            subprocess.Popen(["about-applet.py","About ToaruPaint","applications-painting","/usr/share/icons/48/applications-painting.png",_description])

        def help_browser(action):
            subprocess.Popen(["help-browser.py","painting.trt"])

        def close_picker():
            self.last_color = self.picker.color
            self.picker = None

        def select_color(action):
            if self.picker:
                return
            else:
                self.picker = ColorPickerWindow(self.decorator, close_picker)
                self.picker.draw()

        menus = [
            ("File", [
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Tools", [
                MenuEntryAction("Color",None,select_color,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",help_browser,None),
                MenuEntryDivider(),
                MenuEntryAction("About ToaruPaint","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.menus = {}
        self.hovered_menu = None

        self.buf = yutani.GraphicsBuffer(500,500)
        self.surface = self.buf.get_cairo_surface()
        self.draw_ctx = cairo.Context(self.surface)
        self.draw_ctx.rectangle(0,0,self.surface.get_width(),self.surface.get_height())
        self.draw_ctx.set_source_rgb(1,1,1)
        self.draw_ctx.fill()

        self.hilighted = None
        self.was_drawing = False
        self.line_width = 1.0
        self.curs_x = None
        self.curs_y = None

    def color(self):
        if self.picker:
            return self.picker.color
        else:
            return self.last_color

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(0.5,0.5,0.5)
        ctx.fill()

        ctx.save()
        ctx.translate(0,self.menubar.height)
        ctx.set_source_surface(self.surface,0,0)
        ctx.paint()

        if not self.curs_x is None:
            ctx.arc(self.curs_x,self.curs_y,self.line_width/2,0,2*math.pi)
            ctx.set_line_width(0.5)
            ctx.set_source_rgba(0,0,0,0.7)
            ctx.stroke()
            ctx.arc(self.curs_x,self.curs_y,self.line_width/2-0.5,0,2*math.pi)
            ctx.set_line_width(0.5)
            ctx.set_source_rgba(1,1,1,0.7)
            ctx.stroke()

        ctx.restore()

        self.menubar.draw(ctx,0,0,WIDTH)

        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width < 120 or msg.height < 120:
            self.resize_offer(max(msg.width,120),max(msg.height,120))
            return
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()

    def get_color(self,x,y):
        c = self.buf.get_value(x,y)
        a = (c >> 24) & 0xFF
        r = (c >> 16) & 0xFF
        g = (c >> 8)  & 0xFF
        b = (c) & 0xFF
        return (r,g,b)

    def mouse_event(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()

        if not self.was_drawing:
            self.curs_x = None
            self.curs_y = None
            if x >= 0 and x < w and y >= 0 and y < self.menubar.height:
                self.menubar.mouse_event(msg, x, y)
                return

            if x < 0 or x >= w or y < 0 or y >= h:
                return

            if x >= 0 and x < w and y >= self.menubar.height and y < h:
                if msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
                    if self.picker:
                        self.picker.set_color(*self.get_color(x,y-self.menubar.height))
                    if not self.menus:
                        pass # No context menu at the moment.
                        #menu_entries = [
                        #    MenuEntryAction("Up",None,self.go_up,None),
                        #]
                        #menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)
                    return

            if y < 0: return

        if msg.buttons & yutani.MouseButton.SCROLL_UP:
            self.line_width *= 1.2
        elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
            self.line_width /= 1.2

        redraw = False

        if not (msg.buttons & yutani.MouseButton.BUTTON_LEFT):
            self.was_drawing = False

        if (msg.command == yutani.MouseEvent.DRAG or msg.command == yutani.MouseEvent.DOWN) and msg.buttons & yutani.MouseButton.BUTTON_LEFT:
            self.was_drawing = True
            self.draw_ctx.set_line_cap(cairo.LINE_CAP_ROUND)
            self.draw_ctx.set_line_join(cairo.LINE_JOIN_ROUND)
            self.draw_ctx.set_source_rgb(*self.color())
            self.draw_ctx.set_line_width(self.line_width)
            if msg.command == yutani.MouseEvent.DOWN:
                self.draw_ctx.move_to(0.5+msg.new_x - self.decorator.left_width(), 0.5+msg.new_y - self.decorator.top_height() - self.menubar.height);
            else:
                self.draw_ctx.move_to(0.5+msg.old_x - self.decorator.left_width(), 0.5+msg.old_y - self.decorator.top_height() - self.menubar.height);
            self.draw_ctx.line_to(0.5+msg.new_x - self.decorator.left_width(), 0.5+msg.new_y - self.decorator.top_height() - self.menubar.height);
            self.draw_ctx.stroke()


        self.curs_x = 0.5+msg.new_x - self.decorator.left_width()
        self.curs_y = 0.5+msg.new_y - self.decorator.top_height() - self.menubar.height
        self.draw()

    def keyboard_event(self, msg):
        if msg.event.action != yutani.KeyAction.ACTION_DOWN:
            return # Ignore anything that isn't a key down.
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = PaintingWindow(d)
    window.draw()

    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            window.close()
            break
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            if msg.wid == window.wid:
                window.keyboard_event(msg)
            elif msg.wid in window.menus:
                window.menus[msg.wid].keyboard_event(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            if msg.wid == window.wid:
                if msg.focused == 0 and window.menus:
                    window.focused = 1
                else:
                    window.focused = msg.focused
                window.draw()
            elif msg.wid in window.menus and msg.focused == 0:
                window.menus[msg.wid].leave_menu()
                if not window.menus and window.focused:
                    window.focused = 0
                    window.draw()
            elif window.picker and msg.wid == window.picker.wid:
                window.picker.focused = msg.focused
                window.picker.draw()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            if msg.wid == window.wid:
                window.finish_resize(msg)
            elif window.picker and msg.wid == window.picker.wid:
                window.picker.resize_finish(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_MOVE:
            if msg.wid == window.wid:
                window.x = msg.x
                window.y = msg.y
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            if msg.wid == window.wid:
                window.mouse_event(msg)
            elif msg.wid in window.menus:
                m = window.menus[msg.wid]
                if msg.new_x >= 0 and msg.new_x < m.width and msg.new_y >= 0 and msg.new_y < m.height:
                    window.hovered_menu = m
                elif window.hovered_menu == m:
                    window.hovered_menu = None
                m.mouse_action(msg)
            elif window.picker and msg.wid == window.picker.wid:
                window.picker.mouse_event(msg)

