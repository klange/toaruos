#!/usr/bin/python3
"""
Simple okay/cancel dialog.
"""
import os
import re
import stat
import sys
import time
import fnmatch

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button
from icon_cache import get_icon

import yutani_mainloop

_default_text = "This is a dialog. <b>Formatting is available.</b>"

hilight_border_top = (54/255,128/255,205/255)
hilight_gradient_top = (93/255,163/255,236/255)
hilight_gradient_bottom = (56/255,137/255,220/55)
hilight_border_bottom = (47/255,106/255,167/255)

class DialogButtons(object):

    OKAY_CANCEL = 1
    YES_NO_CANCEL = 2

class DialogWindow(yutani.Window):

    base_width = 500
    base_height = 150

    text_offset = 40

    okay_label = "Okay"
    cancel_label = "Cancel"

    def __init__(self, decorator, title, text, icon='help', buttons=DialogButtons.OKAY_CANCEL, callback=None, cancel_callback=None,window=None,cancel_label=True,close_is_cancel=True):
        super(DialogWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=title, icon=icon, doublebuffer=True)

        if window:
            # Center window
            self.move(window.x+int((window.width-self.width)/2),window.y+int((window.height-self.height)/2))
        else:
            # Center screen
            self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))
        self.decorator = decorator
        self.logo = get_icon(icon,48,'help')
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.base_width-60,self.base_height-self.text_offset,font=self.font)
        self.tr.set_richtext(text)

        if cancel_label is not True:
            self.cancel_label = cancel_label

        self.button_ok = Button(self.okay_label,self.ok_click)
        self.button_cancel = Button(self.cancel_label,self.cancel_click)
        self.buttons = [self.button_ok]
        if self.cancel_label:
            self.buttons.append(self.button_cancel)

        self.close_is_cancel = close_is_cancel

        self.hover_widget = None
        self.down_button = None

        self.callback = callback
        self.cancel_callback = cancel_callback
        self.draw()

    def ok_click(self, button):
        self.close()
        if self.callback:
            self.callback()
        return False

    def cancel_click(self, button):
        self.close()
        if self.cancel_callback:
            self.cancel_callback()
        return False

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(self), self.height - self.decorator.height(self)

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(self), self.decorator.top_height(self))
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.set_source_surface(self.logo,30,30)
        ctx.paint()

        self.tr.resize(WIDTH-90,HEIGHT-self.text_offset)
        self.tr.move(self.decorator.left_width(self) + 90,self.decorator.top_height(self)+self.text_offset)
        self.tr.draw(self)

        self.button_ok.draw(self,ctx,WIDTH-130,HEIGHT-60,100,30)
        if self.cancel_label:
            self.button_cancel.draw(self,ctx,WIDTH-240,HEIGHT-60,100,30)

        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()

    def mouse_event(self, msg):
        decor_event = self.decorator.handle_event(msg)
        if decor_event == yutani.Decor.EVENT_CLOSE:
            if self.close_is_cancel:
                self.cancel_click(None)
            else:
                self.ok_click(None)
            return
        elif decor_event == yutani.Decor.EVENT_RIGHT:
            self.decorator.show_menu(self, msg)
        x,y = msg.new_x - self.decorator.left_width(self), msg.new_y - self.decorator.top_height(self)
        w,h = self.width - self.decorator.width(self), self.height - self.decorator.height(self)

        redraw = False
        if self.down_button:
            if msg.command == yutani.MouseEvent.RAISE or msg.command == yutani.MouseEvent.CLICK:
                if not (msg.buttons & yutani.MouseButton.BUTTON_LEFT):
                    if x >= self.down_button.x and \
                        x < self.down_button.x + self.down_button.width and \
                        y >= self.down_button.y and \
                        y < self.down_button.y + self.down_button.height:
                            self.down_button.focus_enter()
                            if self.down_button.callback(self.down_button):
                                redraw = True
                            self.down_button = None
                    else:
                        self.down_button.focus_leave()
                        self.down_button = None
                        redraw = True

        else:
            button = None
            for b in self.buttons:
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
        if msg.event.action == yutani.KeyAction.ACTION_DOWN:
            if msg.event.key == b'\n':
                self.ok_click(None)


class File(object):

    def __init__(self, path,name=None):
        if not name:
            self.name = os.path.basename(path)
        else:
            self.name = name
        self.path = os.path.normpath(path)
        self.stat = os.stat(path)
        self.y = 0
        self.x = 0
        self.hilight = False
        self.tr = text_region.TextRegion(0,0,400,20)
        self.tr.set_one_line()
        self.tr.set_ellipsis()
        self.tr.set_text(self.name)

    def do_action(self, dialog):
        if self.is_directory:
            dialog.load_directory(self.path)
            dialog.redraw_buf()
            return True
        else:
            dialog.path = self.path
            dialog.ok_click(None)
            return False

    @property
    def is_directory(self):
        return stat.S_ISDIR(self.stat.st_mode)

    @property
    def is_executable(self):
        return stat.S_IXUSR & self.stat.st_mode and not self.is_directory

    @property
    def icon(self):
        if self.is_directory: return get_icon('folder',16)
        if self.is_executable: return get_icon('applications-generic',16)
        return get_icon('file',16) # Need file icon

    @property
    def sortkey(self):
        if self.is_directory: return "___" + self.name
        else: return "zzz" + self.name


class OpenFileDialog(DialogWindow):

    base_width = 500
    base_height = 450
    okay_label = "Open"

    buf = None
    path = None
    icon_width = 16
    unit_height = 20

    def __init__(self, decorator, title, glob=None, callback=None, cancel_callback=None,window=None):
        self.buf = None
        self.path = None
        if glob:
            self.matcher = re.compile(fnmatch.translate(glob))
        else:
            self.matcher = None
        self.tr = None
        self.load_directory(os.getcwd())
        self.redraw_buf()
        self.hilighted = None
        super(OpenFileDialog, self).__init__(decorator,title,"Open...",icon="open",callback=callback,cancel_callback=cancel_callback,window=window)
        self.tr.set_text(self.directory)

    def ok_click(self, button):
        self.close()
        if self.callback:
            self.callback(self.path)
        return False

    def load_directory(self, directory):
        self.directory = os.path.normpath(directory)
        if self.tr:
            self.tr.set_text(self.directory)
        self.files = sorted([File(os.path.join(self.directory,f)) for f in os.listdir(self.directory)],key=lambda x: x.sortkey)
        if self.matcher:
            self.files = [x for x in self.files if x.is_directory or self.matcher.match(x.name)]
        if directory != '/':
            self.files.insert(0,File(os.path.join(self.directory,'..'),'(Go up)'))
        self.scroll_y = 0

    def redraw_buf(self,clips=None):
        if self.buf:
            self.buf.destroy()
        w = 450
        height = self.unit_height
        self.buf = yutani.GraphicsBuffer(w,len(self.files)*height)

        surface = self.buf.get_cairo_surface()
        ctx = cairo.Context(surface)

        if clips:
            for clip in clips:
                ctx.rectangle(clip.x,clip.y,w,height)
            ctx.clip()

        ctx.rectangle(0,0,surface.get_width(),surface.get_height())
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        offset_y = 0

        i = 0
        for f in self.files:
            f.y = offset_y
            if not clips or f in clips:
                tr = f.tr
                tr.move(26,offset_y+2)
                if f.hilight:
                    gradient = cairo.LinearGradient(0,0,0,height-2)
                    gradient.add_color_stop_rgba(0.0,*hilight_gradient_top,1.0)
                    gradient.add_color_stop_rgba(1.0,*hilight_gradient_bottom,1.0)
                    ctx.rectangle(0,offset_y,w,1)
                    ctx.set_source_rgb(*hilight_border_top)
                    ctx.fill()
                    ctx.rectangle(0,offset_y+height-1,w,1)
                    ctx.set_source_rgb(*hilight_border_bottom)
                    ctx.fill()
                    ctx.save()
                    ctx.translate(0,offset_y+1)
                    ctx.rectangle(0,0,w,height-2)
                    ctx.set_source(gradient)
                    ctx.fill()
                    ctx.restore()
                    tr.font.font_color = 0xFFFFFFFF
                else:
                    ctx.rectangle(0,offset_y,w,height)
                    if i % 2:
                        ctx.set_source_rgb(0.9,0.9,0.9)
                    else:
                        ctx.set_source_rgb(1,1,1)
                    ctx.fill()
                    tr.font.font_color = 0xFF000000
                ctx.set_source_surface(f.icon,4,offset_y+2)
                ctx.paint()
                tr.draw(self.buf)
            offset_y += height
            i += 1

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(self), self.height - self.decorator.height(self)

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(self), self.decorator.top_height(self))
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        #ctx.set_source_surface(self.logo,30,30)
        #ctx.paint()

        self.tr.resize(WIDTH,30)
        self.tr.move(self.decorator.left_width(self),self.decorator.top_height(self)+10)
        self.tr.set_alignment(2)
        self.tr.draw(self)

        ctx.save()
        ctx.translate(20, 40)
        ctx.rectangle(0,0,450,HEIGHT-130)
        ctx.set_line_width(2)
        ctx.set_source_rgb(0.7,0.7,0.7)
        ctx.stroke_preserve()
        ctx.set_source_rgb(1,1,1)
        ctx.fill()
        ctx.rectangle(0,0,450,HEIGHT-130)
        ctx.clip()
        text = self.buf.get_cairo_surface()
        ctx.set_source_surface(text,0,self.scroll_y)
        ctx.paint()
        ctx.restore()

        self.button_cancel.draw(self,ctx,WIDTH-130,HEIGHT-60,100,30)
        self.button_ok.draw(self,ctx,WIDTH-240,HEIGHT-60,100,30)

        self.decorator.render(self)
        self.flip()

    def scroll(self, amount):
        w,h = self.width - self.decorator.width(self), self.height - self.decorator.height(self)
        self.scroll_y += amount
        if self.scroll_y > 0:
            self.scroll_y = 0
        top = min(-(self.buf.height - (h-130)),0)
        if self.scroll_y < top:
            self.scroll_y = top


    def mouse_event(self, msg):
        super(OpenFileDialog,self).mouse_event(msg)

        x,y = msg.new_x - self.decorator.left_width(self), msg.new_y - self.decorator.top_height(self)
        w,h = self.width - self.decorator.width(self), self.height - self.decorator.height(self)
        if y < 0: return
        if x < 0 or x >= w: return

        if msg.buttons & yutani.MouseButton.SCROLL_UP:
            self.scroll(30)
            self.draw()
            return
        elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
            self.scroll(-30)
            self.draw()
            return

        offset_y = self.scroll_y + 40

        redraw = []
        hit = False

        if x >= 20 and x < 450+20:
            for f in self.files:
                if offset_y > h: break
                if y >= offset_y and y < offset_y + self.unit_height:
                    if not f.hilight:
                        redraw.append(f)
                        if self.hilighted:
                            redraw.append(self.hilighted)
                            self.hilighted.hilight = False
                        f.hilight = True
                    self.hilighted = f
                    hit = True
                    break
                offset_y += self.unit_height

        if not hit:
            if self.hilighted:
                redraw.append(self.hilighted)
                self.hilighted.hilight = False
                self.hilighted = None

        if self.hilighted:
            if msg.command == yutani.MouseEvent.DOWN:
                if self.hilighted.do_action(self):
                    redraw = []
                    self.redraw_buf()
                    self.draw()

        if redraw:
            self.redraw_buf(redraw)
            self.draw()


if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    def okay(path):
        print("You hit Okay!",path)
        sys.exit(0)

    def cancel():
        print("You hit Cancel!")
        sys.exit(0)

    #window = DialogWindow(d,"Okay/Cancel Dialog","A thing happend!",cancel_callback=cancel,callback=okay)
    window = OpenFileDialog(d,"Open...",glob="*.png",cancel_callback=cancel,callback=okay)

    yutani_mainloop.mainloop()

