#!/usr/bin/python3
"""
Simple okay/cancel dialog.
"""
import os
import sys

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button
from icon_cache import get_icon

import yutani_mainloop

_default_text = "This is a dialog. <b>Formatting is available.</b>"

class DialogButtons(object):

    OKAY_CANCEL = 1
    YES_NO_CANCEL = 2

class DialogWindow(yutani.Window):

    base_width = 500
    base_height = 150

    text_offset = 40

    def __init__(self, decorator, title, text, icon='help', buttons=DialogButtons.OKAY_CANCEL, callback=None, cancel_callback=None,window=None):
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

        self.button_ok = Button("Okay",self.ok_click)
        self.button_cancel = Button("Cancel",self.cancel_click)
        self.buttons = [self.button_ok, self.button_cancel]

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

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.set_source_surface(self.logo,30,30)
        ctx.paint()

        self.tr.resize(WIDTH-90,HEIGHT-self.text_offset)
        self.tr.move(self.decorator.left_width() + 90,self.decorator.top_height()+self.text_offset)
        self.tr.draw(self)

        self.button_cancel.draw(self,ctx,WIDTH-130,HEIGHT-60,100,30)
        self.button_ok.draw(self,ctx,WIDTH-240,HEIGHT-60,100,30)

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
        if self.decorator.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            self.cancel_click(None)
            return
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
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    def okay():
        print("You hit Okay!")
        sys.exit(0)

    def cancel():
        print("You hit Cancel!")
        sys.exit(0)

    window = DialogWindow(d,"Okay/Cancel Dialog","A thing happend!",cancel_callback=cancel,callback=okay)

    yutani_mainloop.mainloop()

