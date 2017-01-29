#!/usr/bin/python3
"""
Fancy clock.
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

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from about_applet import AboutAppletWindow

import yutani_mainloop

app_name = "Clock"
version = "1.0.0"
_description = f"<b>{app_name} {version}</b>\n© 2017 Kevin Lange\n\nAnalog clock widget.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

class BasicWatchFace(object):

    def __init__(self):
        self.font = toaru_fonts.get_cairo_face()

    def draw_background(self, ctx, t):
        ctx.set_line_width(9)
        ctx.set_source_rgb(0,0,0)
        ctx.arc(0,0,100 - 10, 0, 2 * math.pi)
        ctx.stroke_preserve()
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        r2 = 100 - 9
        ctx.set_source_rgb(0,0,0)
        for i in range(12*5):
            theta = 2 * math.pi * (i / (12*5))
            if i % 5 == 0:
                ctx.set_line_width(2)
                r1 = 100 - 20
            else:
                ctx.set_line_width(0.5)
                r1 = 100 - 14
            ctx.move_to(math.sin(theta) * r1, -r1 * math.cos(theta))
            ctx.line_to(math.sin(theta) * r2, -r2 * math.cos(theta))
            ctx.stroke()

    def setup_labels(self, ctx, t):
        ctx.set_font_face(self.font)
        ctx.set_font_size(12)
        ctx.set_source_rgb(0,0,0)

    def draw_labels(self, ctx, t):
        ctx.save()
        label = "12"
        e = ctx.text_extents(label)
        ctx.move_to(-e[2]/2,-72+e[3])
        ctx.show_text(label)
        ctx.fill()
        label = "3"
        e = ctx.text_extents(label)
        ctx.move_to(75-e[2],e[3]/2)
        ctx.show_text(label)
        ctx.fill()
        label = "9"
        e = ctx.text_extents(label)
        ctx.move_to(-75,e[3]/2)
        ctx.show_text(label)
        ctx.fill()
        label = "6"
        e = ctx.text_extents(label)
        ctx.move_to(-e[2]/2,72)
        ctx.show_text(label)
        ctx.fill()

        label = "1"
        e = ctx.text_extents(label)
        ctx.move_to(39-e[2],-63+e[3])
        ctx.show_text(label)
        ctx.fill()
        label = "11"
        e = ctx.text_extents(label)
        ctx.move_to(-39,-63+e[3])
        ctx.show_text(label)
        ctx.fill()

        label = "5"
        e = ctx.text_extents(label)
        ctx.move_to(39-e[2],63)
        ctx.show_text(label)
        ctx.fill()
        label = "7"
        e = ctx.text_extents(label)
        ctx.move_to(-39,63)
        ctx.show_text(label)
        ctx.fill()

        label = "2"
        e = ctx.text_extents(label)
        ctx.move_to(63-e[2],-37+e[3])
        ctx.show_text(label)
        ctx.fill()
        label = "10"
        e = ctx.text_extents(label)
        ctx.move_to(-63,-37+e[3])
        ctx.show_text(label)
        ctx.fill()

        label = "4"
        e = ctx.text_extents(label)
        ctx.move_to(63-e[2],37)
        ctx.show_text(label)
        ctx.fill()
        label = "8"
        e = ctx.text_extents(label)
        ctx.move_to(-63,37)
        ctx.show_text(label)
        ctx.fill()
        ctx.restore()

    def draw_date(self, ctx, t):
        ctx.save()
        ctx.set_font_size(10)
        label = time.strftime('%B %e',t[0])
        e = ctx.text_extents(label)
        ctx.move_to(-e[2]/2,-30)
        ctx.show_text(label)
        ctx.fill()

    def draw_line(self, ctx, thickness, color, a, b, r1, r2):
        theta = (a / b) * 2 * math.pi
        ctx.set_line_width(thickness)
        ctx.set_source_rgb(*color)
        ctx.move_to(math.sin(theta) * r1, -r1 * math.cos(theta))
        ctx.line_to(math.sin(theta) * r2, -r2 * math.cos(theta))
        ctx.stroke()

    def tick(self,t):
        ts = t*t
        tc = ts*t
        return (0.5*tc*ts + -8*ts*ts + 20*tc + -19*ts + 7.5*t);

    def draw_hands(self, ctx, t):
        _,h,m,s = t
        self.draw_line(ctx,3,(0,0,0),h%12+(m+s/60)/60,12,52,-5)
        self.draw_line(ctx,2,(0,0,0),m+s/60,60,86,-10)

        _s = int(60+s-1)+self.tick(s%1)
        self.draw_line(ctx,1,(1,0,0),_s,60,86,-20)
        self.draw_line(ctx,3,(1,0,0),_s,60,-4,-16)

    def draw(self, ctx, t):
        self.draw_background(ctx,t)
        self.setup_labels(ctx,t)
        self.draw_labels(ctx,t)
        self.draw_date(ctx,t)
        self.draw_hands(ctx,t)

class DarkWatchFace(BasicWatchFace):
    def draw_background(self, ctx, t):
        ctx.set_line_width(9)
        ctx.set_source_rgb(1,1,1)
        ctx.arc(0,0,100 - 10, 0, 2 * math.pi)
        ctx.stroke_preserve()
        ctx.set_source_rgb(0,0,0)
        ctx.fill()

        r2 = 100 - 9
        ctx.set_source_rgb(1,1,1)
        for i in range(12*5):
            theta = 2 * math.pi * (i / (12*5))
            if i % 5 == 0:
                ctx.set_line_width(2)
                r1 = 100 - 20
            else:
                ctx.set_line_width(0.5)
                r1 = 100 - 14
            ctx.move_to(math.sin(theta) * r1, -r1 * math.cos(theta))
            ctx.line_to(math.sin(theta) * r2, -r2 * math.cos(theta))
            ctx.stroke()

    def draw_hands(self, ctx, t):
        _,h,m,s = t
        self.draw_line(ctx,3,(1,1,1),h%12+(m+s/60)/60,12,52,-5)
        self.draw_line(ctx,2,(1,1,1),m+s/60,60,86,-10)

        _s = int(60+s-1)+self.tick(s%1)
        self.draw_line(ctx,1,(1,0,0),_s,60,86,-20)
        self.draw_line(ctx,3,(1,0,0),_s,60,-4,-16)

    def setup_labels(self, ctx, t):
        ctx.set_font_face(self.font)
        ctx.set_font_size(12)
        ctx.set_source_rgb(1,1,1)


class ClockWindow(yutani.Window):

    base_width = 200
    base_height = 200

    def __init__(self):
        super(ClockWindow, self).__init__(self.base_width, self.base_height, title="Clock", icon="clock", doublebuffer=True)
        self.move(100,100)
        self.update_shape(yutani.WindowShape.THRESHOLD_CLEAR)

        self.menus = {}

        self.watchfaces = {
            'Default': BasicWatchFace(),
            'Dark': DarkWatchFace(),
        }

        self.watchface = self.watchfaces['Default']


    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        # Clear
        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_source_rgba(0,0,0,0)
        ctx.fill()

        ctx.set_operator(cairo.OPERATOR_OVER)
        ctx.translate(self.width / 2, self.height / 2)
        ctx.scale(self.width / 200, self.height / 200)

        current_time = time.time()
        t = time.localtime(int(current_time))
        s = current_time % 60
        m = t.tm_min
        h = t.tm_hour

        self.watchface.draw(ctx,(t,h,m,s))

        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width != msg.height:
            s = min(msg.width,msg.height)
            self.resize_offer(s,s)
            return
        self.resize_accept(msg.width, msg.height)
        self.reinit()

        self.draw()
        self.resize_done()
        self.flip()

    def exit(self, data):
        sys.exit(0)

    def about(self, data):
        AboutAppletWindow(d,f"About {app_name}","/usr/share/icons/48/clock.png",_description,"clock")


    def mouse_event(self, msg):
        # drag start
        if msg.command == yutani.MouseEvent.DOWN and msg.buttons & yutani.MouseButton.BUTTON_LEFT:
            self.drag_start()

        if msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
            if not self.menus:
                def set_face(x):
                    self.watchface = self.watchfaces[x]
                faces = [MenuEntryAction(x,None,set_face,x) for x in self.watchfaces.keys()]
                menu_entries = [
                    MenuEntrySubmenu("Watchface",faces,icon='clock'),
                    MenuEntryAction(f"About {app_name}","star",self.about,None),
                    MenuEntryDivider(),
                    MenuEntryAction("Exit","exit",self.exit,None),
                ]
                menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)

    def keyboard_event(self, msg):
        if msg.event.action != yutani.KeyAction.ACTION_DOWN:
            return
        if msg.event.key == b'q':
            self.exit(None)


if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor() # Just in case.

    window = ClockWindow()
    window.draw()

    fds = [yutani.yutani_ctx]
    while 1:
        # Poll for events.
        fd = fswait.fswait(fds,20)
        if fd == 0:
            msg = yutani.yutani_ctx.poll()
            yutani_mainloop.handle_event(msg)
            window.draw()
        elif fd == 1:
            # Tick
            window.draw()


