#!/usr/bin/python3
"""
Calculator for ToaruOS
"""
import subprocess
import sys

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button
from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from about_applet import AboutAppletWindow

import yutani_mainloop

import ast
import operator as op

operators = {ast.Add: op.add, ast.Sub: op.sub, ast.Mult: op.mul,
             ast.Div: op.truediv, ast.Pow: op.pow, ast.BitXor: op.xor,
             ast.USub: op.neg}

app_name = "Calculator"
version = "1.0.0"
_description = f"<b>{app_name} {version}</b>\n© 2017-2018 K. Lange\n\nSimple four-function calculator using Python.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

def eval_expr(expr):
    """
    >>> eval_expr('2^6')
    4
    >>> eval_expr('2**6')
    64
    >>> eval_expr('1 + 2*3**(4^5) / (6 + -7)')
    -5.0
    """
    return eval_(ast.parse(expr, mode='eval').body)

def eval_(node):
    if isinstance(node, ast.Num): # <number>
        return node.n
    elif isinstance(node, ast.BinOp): # <left> <operator> <right>
        return operators[type(node.op)](eval_(node.left), eval_(node.right))
    elif isinstance(node, ast.UnaryOp): # <operator> <operand> e.g., -1
        return operators[type(node.op)](eval_(node.operand))
    else:
        raise TypeError("invalid operation")


class CalculatorWindow(yutani.Window):

    base_width = 200
    base_height = 240

    def __init__(self, decorator):
        super(CalculatorWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=app_name, icon="calculator", doublebuffer=True)
        self.move(100,100)
        self.decorator = decorator

        def add_string(button):
            self.add_string(button.text)

        def clear(button):
            self.clear_text()

        def calculate(button):
            self.calculate()

        self.buttons = [
            [Button("C",clear),      None,                   Button("(",add_string), Button(")",add_string)],
            [Button("7",add_string), Button("8",add_string), Button("9",add_string), Button("/",add_string)],
            [Button("4",add_string), Button("5",add_string), Button("6",add_string), Button("*",add_string)],
            [Button("1",add_string), Button("2",add_string), Button("3",add_string), Button("-",add_string)],
            [Button("0",add_string), Button(".",add_string), Button("=",calculate),  Button("+",add_string)],
        ]

        def exit_app(action):
            menus = [x for x in self.menus.values()]
            for x in menus:
                x.definitely_close()
            self.close()
            sys.exit(0)
        def about_window(action):
            AboutAppletWindow(self.decorator,f"About {app_name}","/usr/share/icons/48/calculator.png",_description,"calculator")
        def help_browser(action):
            subprocess.Popen(["help-browser.py","calculator.trt"])
        menus = [
            ("File", [
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",help_browser,None),
                MenuEntryDivider(),
                MenuEntryAction(f"About {app_name}","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.tr = text_region.TextRegion(self.decorator.left_width(self)+5,self.decorator.top_height(self)+self.menubar.height,self.base_width-10,40)
        self.tr.set_font(toaru_fonts.Font(toaru_fonts.FONT_MONOSPACE,18))
        self.tr.set_text("")
        self.tr.set_alignment(1)
        self.tr.set_valignment(2)
        self.tr.set_one_line()
        self.tr.set_ellipsis()

        self.error = False

        self.hover_widget = None
        self.down_button = None

        self.menus = {}
        self.hovered_menu = None


    def calculate(self):
        if self.error or len(self.tr.text) == 0:
            self.tr.set_text("0")
            self.error = False
        try:
            self.tr.set_text(str(eval_expr(self.tr.text)))
        except Exception as e:
            error = str(e)
            if "(" in error:
                error = error[:error.find("(")-1]
            self.tr.set_richtext(f"<i><color 0xFF0000>{e.__class__.__name__}</color>: {error}</i>")
            self.error = True
        self.draw()
        self.flip()

    def add_string(self, text):
        if self.error:
            self.tr.text = ""
            self.error = False
        self.tr.set_text(self.tr.text + text)
        self.draw()
        self.flip()

    def clear_text(self):
        self.error = False
        self.tr.set_text("")
        self.draw()
        self.flip()

    def clear_last(self):
        if self.error:
            self.error = False
            self.tr.set_text("")
        if len(self.tr.text):
            self.tr.set_text(self.tr.text[:-1])
        self.draw()
        self.flip()

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(self), self.height - self.decorator.height(self)

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(self), self.decorator.top_height(self))
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        ctx.rectangle(0,5+self.menubar.height,WIDTH,self.tr.height-10)
        ctx.set_source_rgb(1,1,1)
        ctx.fill()
        self.tr.move(self.decorator.left_width(self)+5,self.decorator.top_height(self)+self.menubar.height)
        self.tr.resize(WIDTH-10, self.tr.height)
        self.tr.draw(self)

        offset_x = 0
        offset_y = self.tr.height + self.menubar.height
        button_height = int((HEIGHT - self.tr.height - self.menubar.height) / len(self.buttons))
        for row in self.buttons:
            button_width = int(WIDTH / len(row))
            for button in row:
                if button:
                    button.draw(self,ctx,offset_x,offset_y,button_width,button_height)
                offset_x += button_width
            offset_x = 0
            offset_y += button_height

        self.menubar.draw(ctx,0,0,WIDTH)
        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width < 200 or msg.height < 200:
            self.resize_offer(max(msg.width,200),max(msg.height,200))
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
        x,y = msg.new_x - self.decorator.left_width(self), msg.new_y - self.decorator.top_height(self)
        w,h = self.width - self.decorator.width(self), self.height - self.decorator.height(self)

        if x >= 0 and x < w and y >= 0 and y < self.menubar.height:
            self.menubar.mouse_event(msg, x, y)
            return

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
            if y > self.tr.height + self.menubar.height and y < h and x >= 0 and x < w:
                row = int((y - self.tr.height - self.menubar.height) / (self.height - self.decorator.height() - self.tr.height - self.menubar.height) * len(self.buttons))
                col = int(x / (self.width - self.decorator.width(self)) * len(self.buttons[row]))
                button = self.buttons[row][col]
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
            else:
                if self.hover_widget:
                    self.hover_widget.focus_leave()
                    redraw = True
                self.hover_widget = None

        if redraw:
            self.draw()

    def keyboard_event(self, msg):
        if msg.event.action != 0x01:
            return # Ignore anything that isn't a key down.
        if msg.event.key in b"0123456789.+-/*()":
            self.add_string(msg.event.key.decode('utf-8'))
        if msg.event.key == b"\n":
            self.calculate()
        if msg.event.key == b"c":
            self.clear_text()
        if msg.event.keycode == 8:
            self.clear_last()
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = CalculatorWindow(d)
    window.draw()

    yutani_mainloop.mainloop()
