#!/usr/bin/python3
"""
Color picker
"""
import colorsys
import math
import sys

import cairo

import yutani

def s(p1,p2,p3):
    return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])

def dist(a,b):
    return math.sqrt((a[0]-b[0])**2+(a[1]-b[1])**2)

def closest_point(pt, v1, v2):
    ap = (pt[0] - v1[0]),(pt[1] - v1[1])
    ab = (v2[0] - v1[0]),(v2[1] - v1[1])
    ab2 = ab[0]**2 + ab[1]**2
    ap_ab = ap[0]*ab[0] + ap[1]*ab[1]
    t = ap_ab / ab2
    return (v1[0] + ab[0] * t, v1[1] + ab[1] * t)

def point_inside(pt, v1, v2, v3):
    a = s(pt,v1,v2)
    b = s(pt,v2,v3)
    c = s(pt,v3,v1)
    return (a < 0) == (b < 0) == (c < 0)

def dist3(a,b):
    return math.sqrt((a[0]-b[0])**2+(a[1]-b[1])**2+(a[2]-b[2])**2)

def frange(x,y,jump):
    while x < y:
        yield x
        x += jump

class ColorPickerWindow(yutani.Window):

    base_width = 200

    def __init__(self, decorator, close_callback):
        super(ColorPickerWindow,self).__init__(self.base_width + decorator.width(), self.base_width + decorator.height(), title="Color Picker", doublebuffer=True)
        self.move(100,100)
        self.decorator = decorator
        self.close_callback = close_callback

        self.hue = math.radians(240)
        self.angle = -self.hue
        self.pat = cairo.MeshGradient()
        self.pat.begin_patch()
        self.pat.move_to(-1.0,0.0)
        self.pat.curve_to(-1.0,0.3886666666666667,-0.7746666666666667,0.742,-0.4226666666666667,0.906)
        self.pat.curve_to(-0.12666666666666668,1.044,0.2173333333333333,1.0293333333333334,0.5,0.866)
        self.pat.curve_to(0.8093333333333331,0.6873333333333332,1.0,0.3573333333333333,1.0,0.0)
        self.pat.set_corner_color_rgb (0, 0, 1, 1)
        self.pat.set_corner_color_rgb (1, 0, 0, 1)
        self.pat.set_corner_color_rgb (2, 1, 0, 1)
        self.pat.set_corner_color_rgb (3, 1, 0, 0)
        self.pat.end_patch()
        self.pat.begin_patch()
        self.pat.move_to(-1.0,0.0)
        self.pat.curve_to(-1.0,-0.3886666666666667,-0.7746666666666667,-0.742,-0.4226666666666667,-0.906)
        self.pat.curve_to(-0.12666666666666668,-1.044,0.2173333333333333,-1.0293333333333334,0.5,-0.866)
        self.pat.curve_to(0.8093333333333331,-0.6873333333333332,1.0,-0.3573333333333333,1.0,0.0)
        self.pat.set_corner_color_rgb (0, 0, 1, 1)
        self.pat.set_corner_color_rgb (1, 0, 1, 0)
        self.pat.set_corner_color_rgb (2, 1, 1, 0)
        self.pat.set_corner_color_rgb (3, 1, 0, 0)
        self.pat.end_patch()

        self.dit_th = 0.0
        self.dit_r = 0.0
        self.color = (0,0,0)

        self.v1,self.v2,self.v3 = (0,0),(0,0),(0,0)

        self.down_in_circle = False
        self.down_in_triangle = False

    def set_color(self,r,g,b):
        return 
        """
        color = (r/255,g/255,b/255)
        h,s,v = colorsys.rgb_to_hsv(*color)
        self.hue = h * (2 * math.pi)
        self.angle = -self.hue

        white_distance = dist3((1,1,1),color)
        black_distance = dist3((0,0,0),color)
        full_distance  = dist3(colorsys.hsv_to_rgb(self.hue/(2*math.pi),1.0,1.0),color)

        print(white_distance,black_distance,full_distance)

        self.draw()
        """

    def calculate_color(self):
        dit_x,dit_y = math.cos(math.radians(self.dit_th)+self.angle)*self.dit_r,math.sin(math.radians(self.dit_th)+self.angle)*self.dit_r

        dit = dit_x,dit_y
        sat_p = closest_point(dit,self.v1,self.v2)
        exp_p = closest_point(dit,self.v1,self.v3)
        m_exp = closest_point(self.v2,self.v1,self.v3)
        m_sat = closest_point(self.v3,self.v1,self.v2)
        white_amount = dist(sat_p,dit)/dist(m_sat,self.v3)
        mix_amount = dist(exp_p,dit)/dist(m_exp,self.v2)
        r,g,b = colorsys.hsv_to_rgb(self.hue/(2*math.pi),1.0,1.0)
        _r = white_amount * 1.0 + mix_amount * r
        _g = white_amount * 1.0 + mix_amount * g
        _b = white_amount * 1.0 + mix_amount * b

        return (_r,_g,_b), dit

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(),self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()
        ctx.scale (WIDTH/2.0, HEIGHT/2.0)
        ctx.translate(1,1)

        # Draw the concentric circles for the hue selection.
        ctx.arc(0.0,0.0,1.0,0,2*math.pi)
        ctx.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
        ctx.arc(0.0,0.0,0.8,0,2*math.pi)
        ctx.set_source (self.pat)
        ctx.fill()

        self.v1 = math.cos(self.angle+4*math.pi/3)*0.8,math.sin(self.angle+4*math.pi/3)*0.8
        self.v2 = math.cos(self.angle+6*math.pi/3)*0.8,math.sin(self.angle+6*math.pi/3)*0.8
        self.v3 = math.cos(self.angle+8*math.pi/3)*0.8,math.sin(self.angle+8*math.pi/3)*0.8

        # Temporary pattern for inner triangle.
        pat2 = cairo.MeshGradient()
        pat2.begin_patch()
        pat2.move_to(*self.v3)
        pat2.line_to(*self.v1)
        pat2.line_to(*self.v2)
        pat2.line_to(*self.v3)
        pat2.set_corner_color_rgb (0, 1, 1, 1)
        pat2.set_corner_color_rgb (1, 0, 0, 0)
        pat2.set_corner_color_rgb (2, *colorsys.hsv_to_rgb(self.hue/(2*math.pi),1.0,1.0))
        pat2.set_corner_color_rgb (3, 1, 1, 1)
        pat2.end_patch()

        ctx.move_to(*self.v3)
        ctx.line_to(*self.v1)
        ctx.line_to(*self.v2)
        ctx.line_to(*self.v3)
        ctx.set_source (pat2)
        ctx.fill()

        ctx.set_line_width(0.04)
        ctx.move_to(*self.v2)
        ctx.line_to(self.v2[0]/0.8,self.v2[1]/0.8)
        ctx.set_source_rgb(0,0,0)
        ctx.stroke()



        self.color, dit = self.calculate_color()

        ctx.arc(-0.85,-0.85,0.1,0,2*math.pi)
        ctx.set_source_rgb(*self.color)
        ctx.fill()
        ctx.set_line_width(0.02)
        ctx.arc(*dit,0.05,0,2*math.pi)
        ctx.set_source_rgb(0,0,0)
        ctx.stroke()


        self.decorator.render(self)
        self.flip()

    def finish_resize(self,msg):
        """Accept a resize."""
        WIDTH, HEIGHT = msg.width - self.decorator.width(), msg.height - self.decorator.height()
        if WIDTH != HEIGHT:
            self.resize_offer(WIDTH+self.decorator.width(),WIDTH+self.decorator.height())
            return
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()

    def mouse_event(self,msg):
        if self.decorator.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            # Close the window when the 'X' button is clicked.
            self.close()
            self.close_callback()
        if msg.buttons & yutani.MouseButton.BUTTON_LEFT:
            WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()
            x = msg.new_x - self.decorator.left_width() - WIDTH/2
            y = msg.new_y - self.decorator.top_height() - HEIGHT/2
            r = math.sqrt(x*x + y*y)/(WIDTH/2)
            a = 0
            if x > 0 and y > 0:
                a = math.degrees(math.atan(y/x))
            if y == 0 and x < 0:
                a = 180
            if x == 0 and y > 0:
                a = 90
            if x == 0 and y < 0:
                a = 90*3
            if x < 0 and y > 0:
                a = 180 + math.degrees(math.atan(y/x))
            if x < 0 and y < 0:
                a = 180 + math.degrees(math.atan(y/x))
            if x > 0 and y < 0:
                a = 360 + math.degrees(math.atan(y/x))
            if msg.command == yutani.MouseEvent.DOWN:
                self.down_in_circle = False
                self.down_in_triangle = False
                if r < 0.8:
                    self.down_in_triangle = True
                elif r <= 1.0:
                    self.down_in_circle = True
            if self.down_in_triangle:
                _x,_y = x/WIDTH*2, y/HEIGHT*2
                if point_inside((_x,_y),self.v1,self.v2,self.v3):
                    self.dit_th = a - math.degrees(self.angle)
                    self.dit_r = r
                    self.draw()
            if self.down_in_circle:
                self.hue = math.radians(360-a)
                self.angle = -self.hue
                self.draw()
            if msg.command == yutani.MouseEvent.RAISE or msg.command == yutani.MouseEvent.CLICK:
                self.down_in_circle = False
                self.down_in_triangel = False

    def keyboard_event(self, msg):
        if __name__ == '__main__':
            if msg.event.action != 0x01:
                return # Ignore anything that isn't a key down.
            if msg.event.key == b'q':
                self.close()
                sys.exit(0)

if __name__ == '__main__':
    # Connect to the server.
    import yutani_mainloop

    yutani.Yutani()
    d = yutani.Decor()

    def on_close():
        sys.exit(0)

    w = ColorPickerWindow(d,on_close)
    w.draw()

    yutani_mainloop.mainloop()
