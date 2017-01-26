#!/usr/bin/python3
"""
Minesweeper clone
"""
import random
import subprocess
import sys

import cairo

import yutani
import text_region
import toaru_fonts

from button import Button, rounded_rectangle
from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from about_applet import AboutAppletWindow
from input_box import TextInputWindow
from dialog import DialogWindow

import yutani_mainloop

version = "0.1.0"
app_name = "Mines"
_description = f"<b>{app_name} {version}</b>\n© 2017 Kevin Lange\n\nClassic logic game.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

class MineButton(Button):

    def __init__(self,action,r,c,is_mine,neighbor_mines):
        super(MineButton,self).__init__("",action)
        self.row = r
        self.col = c
        self.tr = text_region.TextRegion(0,0,10,10)
        self.is_mine = is_mine
        self.tr.set_text("")
        self.tr.set_alignment(2)
        self.tr.set_valignment(2)
        self.width = None
        self.revealed = False
        self.mines = neighbor_mines
        self.flagged = False

    def reveal(self):
        if self.revealed: return
        self.revealed = True
        if self.is_mine:
            self.tr.set_text("X")
        elif self.mines == 0:
            self.tr.set_text("")
        else:
            self.tr.set_text(str(self.mines))

    def set_flagged(self):
        self.flagged = not self.flagged

    def draw(self, window, ctx, x, y, w, h):
        if self.width != w:
            self.x, self.y, self.width, self.height = x, y, w, h
            x_, y_ = ctx.user_to_device(x,y)
            self.tr.move(int(x_)+2,int(y_)+2)
            self.tr.resize(w-4,h-4)
        rounded_rectangle(ctx,x+2,y+2,w-4,h-4,3)
        if self.revealed:
            ctx.set_source_rgb(0.6,0.6,0.6)
        elif self.flagged:
            ctx.set_source_rgb(0.6,0.1,0.1)
        elif self.hilight == 1:
            ctx.set_source_rgb(0.7,0.7,0.7)
        elif self.hilight == 2:
            ctx.set_source_rgb(0.3,0.3,0.3)
        else:
            ctx.set_source_rgb(1,1,1)
        ctx.fill()

        if self.tr.text:
            self.tr.draw(window)

class MinesWindow(yutani.Window):

    base_width = 400
    base_height = 440

    def __init__(self, decorator):
        super(MinesWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=app_name, icon="mines", doublebuffer=True)
        self.move(100,100)
        self.decorator = decorator
        self.button_width = {}
        self.button_height = 0

        def exit_app(action):
            menus = [x for x in self.menus.values()]
            for x in menus:
                x.definitely_close()
            self.close()
            sys.exit(0)
        def about_window(action):
            AboutAppletWindow(self.decorator,f"About {app_name}","/usr/share/icons/48/mines.png",_description,"mines")
        def help_browser(action):
            subprocess.Popen(["help-browser.py","mines.trt"])
        def custom_game(action):
            def input_callback(input_window):
                size = int(input_window.tr.text)
                input_window.close()
                def second_callback(input_window):
                    mines = int(input_window.tr.text)
                    input_window.close()
                    self.new_game((size,mines))

                TextInputWindow(self.decorator,"How many mines?","mines",text="90",callback=second_callback,window=self)
            TextInputWindow(self.decorator,"How wide/tall?","mines",text="20",callback=input_callback,window=self)

        menus = [
            ("File", [
                MenuEntrySubmenu("New Game...",[
                    MenuEntryAction("9×9, 10 mines",None,self.new_game,(9,10)),
                    MenuEntryAction("16×16, 40 mines",None,self.new_game,(16,40)),
                    MenuEntryAction("20×20, 90 mines",None,self.new_game,(20,90)),
                    MenuEntryAction("Custom...",None,custom_game,None),
                ],icon="new"),
                MenuEntryDivider(),
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",help_browser,None),
                MenuEntryDivider(),
                MenuEntryAction(f"About {app_name}","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.tr = text_region.TextRegion(self.decorator.left_width()+5,self.decorator.top_height()+self.menubar.height,self.base_width-10,40)
        self.tr.set_font(toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,18))
        self.tr.set_alignment(2)
        self.tr.set_valignment(2)
        self.tr.set_one_line()
        self.tr.set_ellipsis()


        self.error = False

        self.hover_widget = None
        self.down_button = None

        self.menus = {}
        self.hovered_menu = None
        self.modifiers = 0

        self.new_game((9,10))

    def basic_game(self):
        self.new_game((9,10))

    def new_game(self,action):
        def mine_func(b):
            button = b
            if self.first_click:
                i = 0
                while button.is_mine or button.mines:
                    if i > 30:
                        DialogWindow(self.decorator,app_name,"Failed to generate a board.",callback=self.basic_game,window=self,icon='mines')
                        return
                    self.new_game(action)
                    button = self.buttons[button.row][button.col]
                    i += 1
                self.first_click = False
            if button.flagged:
                return
            if button.is_mine and not button.revealed:
                self.tr.set_text("You lose.")
                for row in self.buttons:
                    for b in row:
                        b.reveal()
                self.draw()
                def new():
                    self.new_game(action)
                DialogWindow(self.decorator,app_name,"Oops, you clicked a mine! Play another game?",callback=new,window=self,icon='mines')
                return
            else:
                if not button.revealed:
                    button.reveal()
                    if button.mines == 0:
                        n = [x for x in check_neighbor_buttons(button.row,button.col) if not x.revealed]
                        while n:
                            b = n.pop()
                            b.reveal()
                            if b.mines == 0:
                                n.extend([x for x in check_neighbor_buttons(b.row,b.col) if not x.revealed and not x in n])
                    self.check_win()

        self.field_size, self.mine_count = action
        self.first_click = True
        self.tr.set_text(f"There are {self.mine_count} mines.")

        self.mines = []
        i = 0
        while len(self.mines) < self.mine_count:
            x,y = random.randrange(self.field_size),random.randrange(self.field_size)
            i += 1
            if not (x,y) in self.mines:
                i = 0
                self.mines.append((x,y))
            if i > 50:
                DialogWindow(self.decorator,app_name,"Failed to generate a board.",callback=self.basic_game,window=self,icon='mines')
                return

        def check_neighbors(r,c):
            n = []
            if r > 0:
                if c > 0: n.append((r-1,c-1))
                n.append((r-1,c))
                if c < self.field_size-1: n.append((r-1,c+1))
            if r < self.field_size-1:
                if c > 0: n.append((r+1,c-1))
                n.append((r+1,c))
                if c < self.field_size-1: n.append((r+1,c+1))
            if c > 0: n.append((r,c-1))
            if c < self.field_size-1: n.append((r,c+1))
            return n

        def check_neighbor_buttons(r,c):
            return [self.buttons[x][y] for x,y in check_neighbors(r,c)]

        self.buttons = []
        for row in range(self.field_size):
            r = []
            for col in range(self.field_size):
                is_mine = (row,col) in self.mines
                neighbor_mines = len([x for x in check_neighbors(row,col) if x in self.mines])
                r.append(MineButton(mine_func,row,col,is_mine,neighbor_mines))
            self.buttons.append(r)


    def check_win(self):
        buttons = []
        for row in self.buttons:
            buttons.extend(row)
        n_flagged = len([x for x in buttons if x.flagged and not x.revealed])
        n_revealed = len([x for x in buttons if x.revealed])
        if n_flagged == self.mine_count and n_revealed + n_flagged == self.field_size ** 2:
            self.tr.set_text("You win!")
            for b in buttons:
                b.reveal()
            def new():
                self.new_game((self.field_size,self.mine_count))
            DialogWindow(self.decorator,app_name,"You won! Play another game?",callback=new,window=self,icon='mines')

    def flag(self,button):
        button.set_flagged()
        self.check_win()
        self.draw()

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.fill()

        self.tr.resize(WIDTH-10, self.tr.height)
        self.tr.draw(self)

        offset_x = 0
        offset_y = self.tr.height + self.menubar.height
        self.button_height = int((HEIGHT - self.tr.height - self.menubar.height) / len(self.buttons))
        i = 0
        for row in self.buttons:
            self.button_width[i] = int(WIDTH / len(row))
            for button in row:
                if button:
                    button.draw(self,ctx,offset_x,offset_y,self.button_width[i],self.button_height)
                offset_x += self.button_width[i]
            offset_x = 0
            offset_y += self.button_height
            i += 1

        self.menubar.draw(ctx,0,0,WIDTH)
        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width < 400 or msg.height < 400:
            self.resize_offer(max(msg.width,400),max(msg.height,400))
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
                            if self.modifiers & yutani.Modifier.MOD_LEFT_CTRL:
                                self.flag(self.down_button)
                            else:
                                self.down_button.callback(self.down_button)
                            self.down_button = None
                            redraw = True
                    else:
                        self.down_button.focus_leave()
                        self.down_button = None
                        redraw = True

        else:
            if y > self.tr.height + self.menubar.height and y < h and x >= 0 and x < w:
                xh = self.button_height * len(self.buttons)
                row = int((y - self.tr.height - self.menubar.height) / (xh) * len(self.buttons))
                if row < len(self.buttons):
                    xw = self.button_width[row] * len(self.buttons[row])
                    col = int(x / (xw) * len(self.buttons[row]))
                    if col < len(self.buttons[row]):
                        button = self.buttons[row][col]
                    else:
                        button = None
                else:
                    button = None
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
        self.modifiers = msg.event.modifiers
        if msg.event.action != 0x01:
            return # Ignore anything that isn't a key down.
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = MinesWindow(d)
    window.draw()

    yutani_mainloop.mainloop()
