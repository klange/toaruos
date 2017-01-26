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

import yutani_mainloop

class ClockWindow(yutani.Window):

    base_width = 200
    base_height = 200

    def __init__(self):
        super(ClockWindow, self).__init__(self.base_width, self.base_height, title="Clock", icon="clock", doublebuffer=True)
        self.move(100,100)
        self.update_shape(yutani.WindowShape.THRESHOLD_CLEAR)
        self.font = toaru_fonts.get_cairo_face()


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

        # Draw the background.
        ctx.set_line_width(9)
        ctx.set_source_rgb(0,0,0)
        ctx.arc(0,0,100 - 10, 0, 2 * math.pi)
        ctx.stroke_preserve()
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        current_time = time.time()
        t = time.localtime(int(current_time))
        s = current_time % 60
        m = t.tm_min
        h = t.tm_hour

        # Draw some labels.
        ctx.set_font_face(self.font)
        ctx.set_font_size(12)
        ctx.set_source_rgb(0,0,0)

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

        ctx.save()
        ctx.set_font_size(10)
        label = time.strftime('%B %e',time.localtime(int(current_time)))
        e = ctx.text_extents(label)
        ctx.move_to(-e[2]/2,-30)
        ctx.show_text(label)
        ctx.fill()

        # Draw the markers around the hours.
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


        def draw_line(thickness, color, a, b, r1, r2):
            theta = (a / b) * 2 * math.pi
            ctx.set_line_width(thickness)
            ctx.set_source_rgb(*color)
            ctx.move_to(math.sin(theta) * r1, -r1 * math.cos(theta))
            ctx.line_to(math.sin(theta) * r2, -r2 * math.cos(theta))
            ctx.stroke()

        draw_line(3,(0,0,0),h%12+(m+s/60)/60,12,52,-5)
        draw_line(2,(0,0,0),m+s/60,60,86,-10)
        draw_line(1,(1,0,0),s,60,86,-20)

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

    def mouse_event(self, msg):
        # drag start
        if msg.command == yutani.MouseEvent.DOWN and msg.buttons & yutani.MouseButton.BUTTON_LEFT:
            self.drag_start()

    def keyboard_event(self, msg):
        if msg.event.action != yutani.KeyAction.ACTION_DOWN:
            return
        if msg.event.key == b'q':
            sys.exit(0)


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


