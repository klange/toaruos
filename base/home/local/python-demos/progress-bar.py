#!/usr/bin/python3
"""
Shows a progress bar.
"""
import math
import os
import sys

import cairo

import yutani
import text_region
import toaru_fonts
import fswait

import yutani_mainloop

def rounded_rectangle(ctx,x,y,w,h,r):
    degrees = math.pi / 180
    ctx.new_sub_path()

    ctx.arc(x + w - r, y + r, r, -90 * degrees, 0 * degrees)
    ctx.arc(x + w - r, y + h - r, r, 0 * degrees, 90 * degrees)
    ctx.arc(x + r, y + h - r, r, 90 * degrees, 180 * degrees)
    ctx.arc(x + r, y + r, r, 180 * degrees, 270 * degrees)
    ctx.close_path()

def draw_progress_bar(ctx,x,y,w,h,percent):
    # Outer
    rounded_rectangle(ctx,x,y,w,h,4)
    ctx.set_source_rgb(192/255,192/255,192/255)
    ctx.fill()

    # Inner
    rounded_rectangle(ctx,x+1,y+1,w-2,h-2,4)
    ctx.set_source_rgb(217/255,217/255,217/255)
    ctx.fill()

    rounded_rectangle(ctx,x+2,y+2,(w-4) * percent,h-4,4)
    ctx.set_source_rgb(0,92/255,229/255)
    ctx.fill()

class ProgressBarWindow(yutani.Window):

    base_width = 350
    base_height = 80
    text_offset = 50

    def __init__(self, decorator, title, icon):
        super(ProgressBarWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=title, icon=icon, doublebuffer=True)
        self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))
        self.decorator = decorator
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.base_width-30,self.base_height-self.text_offset,font=self.font)
        self.tr.set_alignment(2)
        self.progress = 0
        self.total = 1

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        draw_progress_bar(ctx,10,20,WIDTH-20,20,self.progress / self.total)

        percent = int(100 * self.progress / self.total)
        self.tr.set_text(f"{percent}%")
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
        pass

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    title = "Progress" if len(sys.argv) < 2 else sys.argv[1]
    icon  = "default" if len(sys.argv) < 3 else sys.argv[2]

    window = ProgressBarWindow(d,title,icon)
    window.draw()

    fds = [yutani.yutani_ctx,sys.stdin]
    while 1:
        # Poll for events.
        fd = fswait.fswait(fds)

        if fd < 0:
            print("? fswait error.")
        elif fd == 0:
            msg = yutani.yutani_ctx.poll()
            yutani_mainloop.handle_event(msg)
        elif fd == 1:
            status = sys.stdin.readline().strip()
            if status == "done":
                window.close()
                break
            window.progress, window.total = map(int,status.split(" "))
            window.draw()

