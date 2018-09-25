import math

import cairo

import yutani
import text_region
import toaru_fonts

def rounded_rectangle(ctx,x,y,w,h,r):
    degrees = math.pi / 180
    ctx.new_sub_path()

    ctx.arc(x + w - r, y + r, r, -90 * degrees, 0 * degrees)
    ctx.arc(x + w - r, y + h - r, r, 0 * degrees, 90 * degrees)
    ctx.arc(x + r, y + h - r, r, 90 * degrees, 180 * degrees)
    ctx.arc(x + r, y + r, r, 180 * degrees, 270 * degrees)
    ctx.close_path()

def draw_button(ctx,x,y,w,h,hilight):
    """Theme definition for drawing a button."""
    ctx.save()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)

    if hilight == 2:
        rounded_rectangle(ctx, 2 + x, 2 + y, w - 4, h - 4, 2.0)
        ctx.set_source_rgba(134/255,173/255,201/255,1.0)
        ctx.set_line_width(2)
        ctx.stroke()

        rounded_rectangle(ctx, 2 + x, 2 + y, w - 4, h - 4, 2.0)
        ctx.set_source_rgba(202/255,211/255,232/255,1.0)
        ctx.fill()
    else:
        rounded_rectangle(ctx, 2 + x, 2 + y, w - 4, h - 4, 2.0)
        ctx.set_source_rgba(44/255,71/255,91/255,29/255)
        ctx.set_line_width(4)
        ctx.stroke()

        rounded_rectangle(ctx, 2 + x, 2 + y, w - 4, h - 4, 2.0)
        ctx.set_source_rgba(158/255,169/255,177/255,1.0)
        ctx.set_line_width(2)
        ctx.stroke()

        if hilight == 1:
            pat = cairo.LinearGradient(2+x,2+y,2+x,2+y+h-4)
            pat.add_color_stop_rgba(0,1,1,1,1)
            pat.add_color_stop_rgba(1,229/255,229/255,246/255,1)
            rounded_rectangle(ctx,2+x,2+y,w-4,h-4,2.0)
            ctx.set_source(pat)
            ctx.fill()

            pat = cairo.LinearGradient(3+x,3+y,3+x,3+y+h-4)
            pat.add_color_stop_rgba(0,252/255,252/255,254/255,1)
            pat.add_color_stop_rgba(1,212/255,223/255,251/255,1)
            rounded_rectangle(ctx,3+x,3+y,w-5,h-5,2.0)
            ctx.set_source(pat)
            ctx.fill()

        else:
            pat = cairo.LinearGradient(2+x,2+y,2+x,2+y+h-4)
            pat.add_color_stop_rgba(0,1,1,1,1)
            pat.add_color_stop_rgba(1,241/255,241/255,244/255,1)
            rounded_rectangle(ctx,2+x,2+y,w-4,h-4,2.0)
            ctx.set_source(pat)
            ctx.fill()

            pat = cairo.LinearGradient(3+x,3+y,3+x,3+y+h-4)
            pat.add_color_stop_rgba(0,252/255,252/255,254/255,1)
            pat.add_color_stop_rgba(1,223/255,225/255,230/255,1)
            rounded_rectangle(ctx,3+x,3+y,w-5,h-5,2.0)
            ctx.set_source(pat)
            ctx.fill()

    ctx.restore()

class Button(object):

    def __init__(self, text, callback):
        self.text = text
        self.callback = callback
        self.hilight = 0
        self.x, self.y, self.width, self.height = 0,0,0,0

    def draw(self, window, ctx, x, y, w, h):
        self.x, self.y, self.width, self.height = x, y, w, h
        draw_button(ctx,x,y,w,h,self.hilight)

        x_, y_ = ctx.user_to_device(x,y)
        tr = text_region.TextRegion(int(x_),int(y_),w,h)
        tr.set_alignment(2)
        tr.set_valignment(2)
        tr.set_text(self.text)
        tr.draw(window)

    def focus_enter(self):
        self.hilight = 1

    def focus_leave(self):
        self.hilight = 0
