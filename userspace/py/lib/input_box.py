#!/usr/bin/python3
"""
Shows an input box.
"""
import math
import os
import sys

import cairo

import yutani
import text_region
import toaru_fonts
import fswait

from button import Button

import yutani_mainloop

def rounded_rectangle(ctx,x,y,w,h,r):
    degrees = math.pi / 180
    ctx.new_sub_path()

    ctx.arc(x + w - r, y + r, r, -90 * degrees, 0 * degrees)
    ctx.arc(x + w - r, y + h - r, r, 0 * degrees, 90 * degrees)
    ctx.arc(x + r, y + h - r, r, 90 * degrees, 180 * degrees)
    ctx.arc(x + r, y + r, r, 180 * degrees, 270 * degrees)
    ctx.close_path()

def draw_input_box(ctx,x,y,w,h,focused):
    # Outer
    rounded_rectangle(ctx,x,y,w,h,4)
    if focused:
        ctx.set_source_rgb(0x8E/0xFF,0xD8/0xFF,1)
    else:
        ctx.set_source_rgb(192/255,192/255,192/255)
    ctx.fill()

    # Inner
    rounded_rectangle(ctx,x+1,y+1,w-2,h-2,4)
    if focused:
        ctx.set_source_rgb(246/255,246/255,246/255)
    else:
        ctx.set_source_rgb(234/255,234/255,234/255)
    ctx.fill()


class TextInputWindow(yutani.Window):

    base_width = 500
    base_height = 120

    text_offset = 22

    okay_label = "Okay"
    cancel_label = "Cancel"

    def __init__(self, decorator, title, icon, text="", text_changed=None, callback=None,window=None):
        super(TextInputWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=title, icon=icon, doublebuffer=True)
        if window:
            # Center window
            self.move(window.x+int((window.width-self.width)/2),window.y+int((window.height-self.height)/2))
        else:
            # Center screen
            self.move(int((yutani.yutani_ctx._ptr.contents.display_width-self.width)/2),int((yutani.yutani_ctx._ptr.contents.display_height-self.height)/2))
        self.decorator = decorator
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.base_width-30,self.base_height-self.text_offset,font=self.font)
        self.tr.set_one_line()
        self.tr.break_all = True
        self.tr.set_text(text)
        self.is_focused = False
        self.cursor_index = len(self.tr.text)
        self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
        self.text_changed = text_changed
        self.callback = callback
        self.ctrl_chars = [' ','/']

        self.button_ok = Button(self.okay_label,self.ok_click)
        self.button_cancel = Button(self.cancel_label,self.cancel_click)
        self.buttons = [self.button_ok, self.button_cancel]

        self.hover_widget = None
        self.down_button = None


    def cancel_click(self, button):
        self.close()
        if __name__ == '__main__':
            sys.exit(0)

    def ok_click(self, button):
        if self.callback:
            self.callback(self)

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        draw_input_box(ctx,10,20,WIDTH-20,20,self.is_focused)

        self.tr.resize(WIDTH-30,HEIGHT-self.text_offset)
        self.tr.move(self.decorator.left_width() + 15,self.decorator.top_height()+self.text_offset)
        self.tr.draw(self)

        if self.cursor_x is not None:
            ctx.rectangle(self.cursor_x + 15, 23, 1, 15)
            ctx.set_source_rgb(0,0,0)
            ctx.fill()

        self.button_ok.draw(self,ctx,WIDTH-130,HEIGHT-60,100,30)
        self.button_cancel.draw(self,ctx,WIDTH-240,HEIGHT-60,100,30)

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
        if self.decorator.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            self.cancel_click(None)
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

        if x >= 10 and x < 10 + w - 20 and y >= 20 and y < 20 + 20:
            changed = not self.is_focused
            self.is_focused = True
            if msg.command == yutani.MouseEvent.CLICK:
                u,l = self.tr.pick(msg.new_x,msg.new_y)
                if u:
                    changed = True
                    self.cursor_x = l[1]
                    self.cursor_index = l[3]
                    if (l[2]-l[1]) > u.width / 2:
                        self.cursor_x += u.width
                        self.cursor_index += 1
                elif l:
                    changed = True
                    self.cursor_x = l[1]
                    self.cursor_index = l[3]
        else:
            changed = self.is_focused
            self.is_focused = False

        if changed or redraw:
            self.draw()

    def keyboard_event(self, msg):
        if msg.event.action == yutani.KeyAction.ACTION_DOWN:
            if self.cursor_x is not None:
                if msg.event.key == b'\x08':
                    if msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL:
                        new_c = self.cursor_index
                        while new_c > 0 and self.tr.text[new_c-1] in self.ctrl_chars:
                            new_c -= 1
                        while new_c > 0 and self.tr.text[new_c-1] not in self.ctrl_chars:
                            new_c -= 1
                        text = self.tr.text
                        before = text[:new_c]
                        after = text[self.cursor_index:]
                        self.tr.set_text(before + after)
                        self.cursor_index = new_c
                        self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                        if self.text_changed:
                            self.text_changed(self)
                        self.draw()
                    else:
                        if self.cursor_index > 0:
                            text = self.tr.text
                            before = text[:self.cursor_index-1]
                            after = text[self.cursor_index:]
                            self.tr.set_text(before + after)
                            self.cursor_index -= 1
                            self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                            if self.text_changed:
                                self.text_changed(self)
                            self.draw()
                elif msg.event.keycode == yutani.Keycode.ARROW_LEFT:
                    if msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL:
                        while self.cursor_index > 0 and self.tr.text[self.cursor_index-1] in self.ctrl_chars:
                            self.cursor_index -= 1
                        while self.cursor_index > 0 and self.tr.text[self.cursor_index-1] not in self.ctrl_chars:
                            self.cursor_index -= 1
                        self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                        self.draw()
                    else:
                        if self.cursor_index > 0:
                            self.cursor_index -= 1
                            self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                            self.draw()
                elif msg.event.keycode == yutani.Keycode.ARROW_RIGHT:
                    if msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL:
                        while self.cursor_index < len(self.tr.text) and self.tr.text[self.cursor_index] in self.ctrl_chars:
                            self.cursor_index += 1
                        while self.cursor_index < len(self.tr.text) and self.tr.text[self.cursor_index] not in self.ctrl_chars:
                            self.cursor_index += 1
                        self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                        self.draw()
                    else:
                        if self.cursor_index < len(self.tr.text):
                            self.cursor_index += 1
                            self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                            self.draw()
                elif msg.event.keycode == yutani.Keycode.END:
                    self.cursor_index = len(self.tr.text)
                    self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                    self.draw()
                elif msg.event.keycode == yutani.Keycode.HOME:
                    self.cursor_index = 0
                    self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                    self.draw()
                elif msg.event.keycode == yutani.Keycode.DEL:
                    if msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL:
                        new_c = self.cursor_index
                        while new_c < len(self.tr.text) and self.tr.text[new_c] in self.ctrl_chars:
                            new_c += 1
                        while new_c < len(self.tr.text) and self.tr.text[new_c] not in self.ctrl_chars:
                            new_c += 1
                        text = self.tr.text
                        before = text[:self.cursor_index]
                        after = text[new_c+1:]
                        self.tr.set_text(before + after)
                        if self.text_changed:
                            self.text_changed(self)
                        self.draw()
                    else:
                        if self.cursor_index < len(self.tr.text):
                            text = self.tr.text
                            before = text[:self.cursor_index]
                            after = text[self.cursor_index+1:]
                            self.tr.set_text(before + after)
                            if self.text_changed:
                                self.text_changed(self)
                            self.draw()
                elif msg.event.key == b'\n':
                    self.ok_click(None)
                elif msg.event.key != b'\x00':
                    text = self.tr.text
                    before = text[:self.cursor_index]
                    after = text[self.cursor_index:]
                    self.tr.set_text(before + msg.event.key.decode('utf-8') + after)
                    self.cursor_index += 1
                    self.cursor_x = self.tr.get_offset_at_index(self.cursor_index)[1][1]
                    if self.text_changed:
                        self.text_changed(self)
                    self.draw()

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    title = "Input Box" if len(sys.argv) < 2 else sys.argv[1]
    icon  = "default" if len(sys.argv) < 3 else sys.argv[2]

    def print_text(text_box):
        print(text_box.tr.text)
        sys.exit(0)

    window = TextInputWindow(d,title,icon,callback=print_text)
    window.draw()

    yutani_mainloop.mainloop()

