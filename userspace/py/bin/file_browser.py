#!/usr/bin/python3
"""
File Browser
"""
import os
import math
import stat
import sys
import subprocess

import cairo

import yutani
import text_region
import toaru_fonts

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from icon_cache import get_icon
from about_applet import AboutAppletWindow
from input_box import TextInputWindow
from dialog import DialogWindow

import yutani_mainloop

app_name = "File Browser"
version = "1.0.0"
_description = f"<b>{app_name} {version}</b>\n© 2017 Kevin Lange\n\nFile system navigator.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

class File(object):

    def __init__(self, path, window):
        self.path = path
        self.name = os.path.basename(path)
        self.stat = os.stat(path)
        self.hilight = False
        self.window = window
        self.tr = text_region.TextRegion(0,0,100,20)
        self.tr.set_alignment(2)
        self.tr.set_ellipsis()
        self.tr.set_text(self.name)
        self.x = 0
        self.y = 0

    @property
    def is_directory(self):
        return stat.S_ISDIR(self.stat.st_mode)

    @property
    def is_executable(self):
        return stat.S_IXUSR & self.stat.st_mode and not self.is_directory

    @property
    def icon(self):
        if self.is_directory: return get_icon('folder',48)
        if self.is_executable: return get_icon(self.name,48)
        return get_icon('file',48) # Need file icon

    def do_action(self):
        if self.is_directory:
            self.window.load_directory(self.path)
            self.window.draw()
        elif self.is_executable:
            subprocess.Popen([self.path])
        elif self.name.endswith('.png'):
            subprocess.Popen(['painting.py',self.path])
        elif self.name.endswith('.pdf') and os.path.exists('/usr/bin/pdfviewer.py'):
            subprocess.Popen(['pdfviewer.py',self.path])
        elif self.name.endswith('.pdf') and os.path.exists('/usr/bin/pdfviewer'):
            subprocess.Popen(['pdfviewer',self.path])
        # Nothing to do.

    @property
    def sortkey(self):
        if self.is_directory: return "___" + self.name
        else: return "zzz" + self.name


class FileBrowserWindow(yutani.Window):

    base_width = 400
    base_height = 300

    def __init__(self, decorator, path):
        super(FileBrowserWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=app_name, icon="folder", doublebuffer=True)
        self.move(100,100)
        self.x = 100
        self.y = 100
        self.decorator = decorator

        def exit_app(action):
            menus = [x for x in self.menus.values()]
            for x in menus:
                x.definitely_close()
            self.close()
            sys.exit(0)
        def about_window(action):
            AboutAppletWindow(self.decorator,f"About {app_name}","/usr/share/icons/48/folder.png",_description,"folder")
        def help_browser(action):
            subprocess.Popen(["help-browser.py","file_browser.trt"])

        def input_path(action):
            def input_callback(input_window):
                text = input_window.tr.text
                input_window.close()
                self.load_directory(text)
            TextInputWindow(self.decorator,"Open directory...","open",text=self.path,callback=input_callback,window=self)

        menus = [
            ("File", [
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Go", [
                MenuEntryAction("Path...","open",input_path,None),
                MenuEntryDivider(),
                MenuEntryAction("Home","home",self.load_directory,os.environ.get("HOME")),
                MenuEntryAction("File System",None,self.load_directory,"/"),
                MenuEntryAction("Up","up",self.go_up,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",help_browser,None),
                MenuEntryDivider(),
                MenuEntryAction(f"About {app_name}","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.hover_widget = None
        self.down_button = None

        self.menus = {}
        self.hovered_menu = None

        self.buf = None
        self.load_directory(path)
        self.hilighted = None

    def go_up(self, action):
        self.load_directory(os.path.abspath(os.path.join(self.path,'..')))
        self.draw()

    def load_directory(self, path):
        if not os.path.exists(path):
            DialogWindow(self.decorator,app_name,f"The path <mono>{path}</mono> could not be opened. (Not found)",window=self,icon='folder')
            return
        if not os.path.isdir(path):
            DialogWindow(self.decorator,app_name,f"The path <mono>{path}</mono> could not be opened. (Not a directory)",window=self,icon='folder')
            return
        path = os.path.normpath(path)
        self.path = path
        title = "/" if path == "/" else os.path.basename(path)
        self.set_title(f"{title} - {app_name}",'folder')

        self.files = sorted([File(os.path.join(path,f), self) for f in os.listdir(path)], key=lambda x: x.sortkey)
        self.scroll_y = 0
        self.hilighted = None
        self.redraw_buf()

    def redraw_buf(self,icons=None):
        if self.buf:
            self.buf.destroy()
        w = self.width - self.decorator.width()
        files_per_row = int(w / 100)
        self.buf = yutani.GraphicsBuffer(w,math.ceil(len(self.files)/files_per_row)*100)

        surface = self.buf.get_cairo_surface()
        ctx = cairo.Context(surface)

        if icons:
            for icon in icons:
                ctx.rectangle(icon.x,icon.y,100,100)
            ctx.clip()


        ctx.rectangle(0,0,surface.get_width(),surface.get_height())
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        offset_x = 0
        offset_y = 0

        for f in self.files:
            if not icons or f in icons:
                x_, y_ = ctx.user_to_device(0,0)
                f.tr.move(offset_x,offset_y+60)
                f.tr.draw(self.buf)
                ctx.set_source_surface(f.icon,offset_x + 26,offset_y+10)
                ctx.paint_with_alpha(1.0 if not f.hilight else 0.7)
            f.x = offset_x
            f.y = offset_y
            offset_x += 100
            if offset_x + 100 > surface.get_width():
                offset_x = 0
                offset_y += 100

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        ctx.save()
        ctx.translate(0,self.menubar.height)
        text = self.buf.get_cairo_surface()
        ctx.set_source_surface(text,0,self.scroll_y)
        ctx.paint()
        ctx.restore()

        self.menubar.draw(ctx,0,0,WIDTH)

        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width < 120 or msg.height < 120:
            self.resize_offer(max(msg.width,120),max(msg.height,120))
            return
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.redraw_buf()
        self.draw()
        self.resize_done()
        self.flip()

    def scroll(self, amount):
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()
        files_per_row = int(w / 100)
        rows_total = math.ceil(len(self.files) / files_per_row)
        rows_visible = int((h - 24) / 100)
        rows = rows_total - rows_visible
        if rows < 0: rows = 0
        self.scroll_y += amount
        if self.scroll_y > 0:
            self.scroll_y = 0
        if self.scroll_y < -100 * rows:
            self.scroll_y = -100 * rows

    def mouse_event(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()

        if x >= 0 and x < w and y >= 0 and y < self.menubar.height:
            self.menubar.mouse_event(msg, x, y)
            return

        if msg.buttons & yutani.MouseButton.SCROLL_UP:
            self.scroll(30)
            self.draw()
            return
        elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
            self.scroll(-30)
            self.draw()
            return

        if x >= 0 and x < w and y >= self.menubar.height and y < h:
            if msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
                if not self.menus:
                    menu_entries = [
                        MenuEntryAction("Up","up",self.go_up,None),
                    ]
                    menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)
                return

        if y < 0: return

        offset_x = 0
        offset_y = self.scroll_y + self.menubar.height

        redraw = []

        files_per_row = int(w / 100)
        rows_total = math.ceil(len(self.files) / files_per_row)
        skip_files = files_per_row * (int(-offset_y / 100))
        offset_y += int(-offset_y/100) * 100

        hit = False
        for f in self.files[skip_files:]:
            if offset_y > h: break
            if offset_y > -100:
                if x >= offset_x and x < offset_x + 100 and y >= offset_y and y < offset_y + 100:
                    if not f.hilight:
                        redraw.append(f)
                        if self.hilighted:
                            redraw.append(self.hilighted)
                            self.hilighted.hilight = False
                        f.hilight = True
                    self.hilighted = f
                    hit = True
                    break
            offset_x += 100
            if offset_x + 100 > w:
                offset_x = 0
                offset_y += 100
        if not hit:
            if self.hilighted:
                redraw.append(self.hilighted)
                self.hilighted.hilight = False
                self.hilighted = None

        if self.hilighted:
            if msg.command == yutani.MouseEvent.DOWN:
                self.hilighted.do_action()

        if redraw:
            self.redraw_buf(redraw)
            self.draw()

    def keyboard_event(self, msg):
        if msg.event.action != yutani.KeyAction.ACTION_DOWN:
            return # Ignore anything that isn't a key down.
        if msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = FileBrowserWindow(d,os.environ.get('HOME','/') if len(sys.argv) < 2 else sys.argv[1])
    window.draw()

    yutani_mainloop.mainloop()
