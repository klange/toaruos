#!/usr/bin/python3
"""
'Select Wallpaper' app
"""
import configparser
import os
import signal
import sys

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button
import yutani_mainloop

class SideButton(Button):

    def draw(self, window, ctx, x, y, w, h):
        self.x, self.y, self.width, self.height = x, y, w, h

        if self.hilight == 0:
            return

        ctx.rectangle(x,y,w,h)
        if self.hilight == 1:
            ctx.set_source_rgba(0,0,0,0.7)
        elif self.hilight == 2:
            ctx.set_source_rgba(1,1,1,0.7)
        ctx.fill()


        x_, y_ = ctx.user_to_device(x,y)
        font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,18,0xFFFFFFFF)
        tr = text_region.TextRegion(int(x_),int(y_),w,h,font=font)
        tr.set_alignment(2)
        tr.set_valignment(2)
        tr.set_text(self.text)
        tr.draw(window)

class WallpaperSelectorWindow(yutani.Window):

    base_width = 640
    base_height = 480
    fallback = '/usr/share/wallpapers/default'

    def __init__(self, decorator):
        super(WallpaperSelectorWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title="Select Wallpaper", icon="select-wallpaper", doublebuffer=True)
        self.move(100,100)
        self.decorator = decorator

        self.find_wallpapers()
        self.read_wallpaper()
        self.load_wallpaper()

        with open('/tmp/.wallpaper.pid','r') as f:
            self.wallpaper_pid = int(f.read().strip())

        if self.path in self.wallpapers:
            self.index = self.wallpapers.index(self.path)
        else:
            self.index = -2

        def save(button):
            with open(f'{os.environ["HOME"]}/.desktop.conf','w') as f:
                f.write(f"wallpaper={self.path}\n")
            os.kill(self.wallpaper_pid, signal.SIGUSR1)

        def exit(button):
            self.close()
            sys.exit(0)

        def previous_wallpaper(button):
            self.index -= 1
            if self.index < 0:
                self.index = len(self.wallpapers)-1
            self.update()

        def next_wallpaper(button):
            self.index += 1
            if self.index == len(self.wallpapers) or self.index == -1:
                self.index = 0
            self.update()

        self.buttons = [
            Button("Apply",save),
            Button("Exit",exit),
        ]

        self.previous_button = SideButton("❰",previous_wallpaper)
        self.next_button = SideButton("❱",next_wallpaper)
        self.extra_buttons = [
            self.previous_button,
            self.next_button,
        ]

        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,13,0xFFFFFFFF)
        self.font.set_shadow((0xFF000000, 2, 1, 1, 3.0))
        self.tr = text_region.TextRegion(self.decorator.left_width()+5,self.decorator.top_height()+5,self.base_width-10,40,font=self.font)
        self.tr.set_alignment(2)
        self.tr.set_one_line()
        self.tr.set_text(self.path)

        self.error = False

        self.hover_widget = None
        self.down_button = None

    def update(self):
        self.path = self.wallpapers[self.index]
        self.tr.set_text(self.path)
        self.load_wallpaper()
        self.draw()

    def load_directory(self, path):
        if not os.path.exists(path):
            return []

        return [os.path.join(path,x) for x in os.listdir(path) if x.endswith('.png')]

    def find_wallpapers(self):
        self.wallpapers = []

        self.wallpapers.extend(self.load_directory('/usr/share/wallpapers'))
        self.wallpapers.extend(self.load_directory('/tmp/wallpapers'))

    def read_wallpaper(self):
        home = os.environ['HOME']
        conf = f'{home}/.desktop.conf'
        if not os.path.exists(conf):
            self.path = self.fallback
        else:
            with open(conf,'r') as f:
                conf_str = '[desktop]\n' + f.read()
            c = configparser.ConfigParser()
            c.read_string(conf_str)
            self.path = c['desktop'].get('wallpaper',self.fallback)

    def load_wallpaper(self):
        self.wallpaper = cairo.ImageSurface.create_from_png(self.path)

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.rectangle(0,0,WIDTH,HEIGHT-50)
        ctx.set_source_rgb(0,0,0)
        ctx.fill()

        ctx.save()
        x = WIDTH / self.wallpaper.get_width()
        y = (HEIGHT-50) / self.wallpaper.get_height()

        nh = int(x * self.wallpaper.get_height())
        nw = int(y * self.wallpaper.get_width())

        if (nw < WIDTH):
            ctx.translate((WIDTH - nw) / 2, 0)
            ctx.scale(y,y)
        else:
            ctx.translate(0,(HEIGHT - 50 - nh) / 2)
            ctx.scale(x,x)

        ctx.set_source_surface(self.wallpaper,0,0)
        ctx.paint()
        ctx.restore()

        self.tr.resize(WIDTH,self.tr.height)
        self.tr.draw(self)

        side_width = int(WIDTH/10)
        self.previous_button.draw(self,ctx,0,0,side_width,HEIGHT-50)
        self.next_button.draw(self,ctx,WIDTH-side_width,0,side_width,HEIGHT-50)

        offset_x = 20
        offset_y = HEIGHT-40
        button_height = 30
        button_width = 100
        button_pad = int((WIDTH - (button_width * len(self.buttons)) - (40))/(len(self.buttons)-1))

        for button in self.buttons:
            if button:
                button.draw(self,ctx,offset_x,offset_y,button_width,button_height)
            offset_x += button_width + button_pad

        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        _w = len(self.buttons) * 100 + 40 + self.decorator.width()
        if msg.width < _w or msg.height < 120:
            self.resize_offer(max(msg.width,_w),max(msg.height,120))
            return
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
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
            button = None
            for b in self.buttons + self.extra_buttons:
                if x >= b.x and x < b.x + b.width and y >= b.y and y < b.y + b.height:
                    button = b
                    break
            if button != self.hover_widget:
                if button:
                    button.focus_enter()
                    redraw = True
                if self.hover_widget:
                    self.hover_widget.focus_leave()
                    redraw = True
                self.hover_widget = button

            if msg.command == yutani.MouseEvent.DOWN:
                if button:
                    button.hilight = 2
                    self.down_button = button
                    redraw = True
            if not button:
                if self.hover_widget:
                    self.hover_widget.focus_leave()
                    redraw = True
                self.hover_widget = None

        if redraw:
            self.draw()

    def keyboard_event(self, msg):
        if msg.event.action != 0x01:
            return # Ignore anything that isn't a key down.
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = WallpaperSelectorWindow(d)
    window.draw()

    yutani_mainloop.mainloop()


