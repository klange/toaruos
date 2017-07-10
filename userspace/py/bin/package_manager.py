#!/usr/bin/python3
"""
Package Manager
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
import msk

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from icon_cache import get_icon
from about_applet import AboutAppletWindow
from dialog import DialogWindow

import yutani_mainloop

app_name = "Package Manager"
version = "1.0.0"
_description = f"<b>{app_name} {version}</b>\n© 2017 Kevin Lange\n\nBrowse and install software packages.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

hilight_border_top = (54/255,128/255,205/255)
hilight_gradient_top = (93/255,163/255,236/255)
hilight_gradient_bottom = (56/255,137/255,220/55)
hilight_border_bottom = (47/255,106/255,167/255)

_local_index = None
_manifest = None

package_height = 50

def install(name):
    msk.needs_local_cache()
    all_packages = msk.resolve_dependencies([name], _local_index, _manifest)

    for name in all_packages:
        msk.fetch_package(name, _manifest)

    for name in all_packages:
        msk.install_fetched_package(name, _manifest, _local_index, [name])

    msk.commit_local_index(_local_index)
    msk.signal_desktop()

class Package(object):

    def __init__(self, name):
        self.name = name
        self.y = 0
        self.x = 0
        self.hilight = False

    @property
    def installed(self):
        return self.name in _local_index

    @property
    def description(self):
        return _manifest[self.name]['description'].replace('\n',' ')

    @property
    def version(self):
        a,b,c= _manifest[self.name]['version']
        return f"{a}.{b}.{c}"

    @property
    def friendly_name(self):
        return _manifest[self.name]['friendly-name']

    @property
    def text(self):
        return f"<h2><b>{self.friendly_name}</b> - {self.version}</h2>\n{self.description}"

    def do_action(self):
        if self.installed:
            return False
        def do_it():
            install(self.name)
            window.redraw_buf()
            window.draw()
        DialogWindow(window.decorator,f"Install {self.name}?",f"The package `{self.name}` will now be installed. Continue?",callback=do_it,window=window,icon='package')
        return True

class NoPackages(object):

    def __init__(self):
        self.name = "No available packages or could not reach package server."
        self.y = 0
        self.x = 0
        self.hilight = False
        self.installed = False
        self.version = ''
        self.text = self.name

    def do_action(self):
        pass

class PackageManagerWindow(yutani.Window):

    base_width = 400
    base_height = 300

    def __init__(self, decorator):
        super(PackageManagerWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=app_name, icon="package", doublebuffer=True)
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
            AboutAppletWindow(self.decorator,f"About {app_name}","/usr/share/icons/48/package.png",_description,"package")
        def help_browser(action):
            subprocess.Popen(["help-browser.py","packages.trt"])
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

        self.menus = {}
        self.hovered_menu = None

        self.scroll_y = 0
        self.hilighted = None
        self.buf = None
        self.hilighted = None

    def load_packages(self):
        self.packages = sorted([Package(name) for name in _manifest.keys()],key=lambda x: x.name)

    def redraw_buf(self,clips=None):
        if self.buf:
            self.buf.destroy()
        w = self.width - self.decorator.width()
        self.buf = yutani.GraphicsBuffer(w,len(self.packages)*package_height)

        surface = self.buf.get_cairo_surface()
        ctx = cairo.Context(surface)

        if clips:
            for clip in clips:
                ctx.rectangle(clip.x,clip.y,w,package_height)
            ctx.clip()

        ctx.rectangle(0,0,surface.get_width(),surface.get_height())
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        offset_y = 0

        for f in self.packages:
            f.y = offset_y
            if not clips or f in clips:
                tr = text_region.TextRegion(54,offset_y+4,w-54,package_height-4)
                tr.line_height = 20
                if f.hilight:
                    gradient = cairo.LinearGradient(0,0,0,18)
                    gradient.add_color_stop_rgba(0.0,*hilight_gradient_top,1.0)
                    gradient.add_color_stop_rgba(1.0,*hilight_gradient_bottom,1.0)
                    ctx.rectangle(0,offset_y+4,w,1)
                    ctx.set_source_rgb(*hilight_border_top)
                    ctx.fill()
                    ctx.rectangle(0,offset_y+package_height-1,w,1)
                    ctx.set_source_rgb(*hilight_border_bottom)
                    ctx.fill()
                    ctx.save()
                    ctx.translate(0,offset_y+4+1)
                    ctx.rectangle(0,0,w,package_height-6)
                    ctx.set_source(gradient)
                    ctx.fill()
                    ctx.restore()
                    tr.font.font_color = 0xFFFFFFFF
                else:
                    ctx.rectangle(0,offset_y+4,w,package_height-4)
                    ctx.set_source_rgb(1,1,1)
                    ctx.fill()
                    tr.font.font_color = 0xFF000000
                if f.installed:
                    package_icon = get_icon('package',48)
                    ctx.set_source_surface(package_icon,2,1+offset_y)
                    ctx.paint()
                tr.set_richtext(f.text)
                tr.set_one_line()
                tr.set_ellipsis()
                tr.draw(self.buf)
            offset_y += package_height

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
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height() - self.menubar.height
        self.scroll_y += amount
        if self.scroll_y > 0:
            self.scroll_y = 0
        max_scroll = self.buf.height - h if h < self.buf.height else 0
        if self.scroll_y < -max_scroll:
            self.scroll_y = -max_scroll

    def mouse_event(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()

        if x >= 0 and x < w and y >= 0 and y < self.menubar.height:
            self.menubar.mouse_event(msg, x, y)
            return

        if x < 0 or x >= w or y < 0 or y >= h:
            return

        if x >= 0 and x < w and y >= self.menubar.height and y < h:
            if msg.buttons & yutani.MouseButton.SCROLL_UP:
                self.scroll(30)
                self.draw()
                return
            elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
                self.scroll(-30)
                self.draw()
                return

            if msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
                if not self.menus:
                    pass # no context menu at the moment
                    #menu_entries = [
                    #    MenuEntryAction("Up",None,self.go_up,None),
                    #]
                    #menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)
                return

        if y < 0: return

        offset_y = self.scroll_y + self.menubar.height

        redraw = []
        hit = False

        for f in self.packages:
            if offset_y > h: break
            if y >= offset_y and y < offset_y + package_height:
                if not f.hilight:
                    redraw.append(f)
                    if self.hilighted:
                        redraw.append(self.hilighted)
                        self.hilighted.hilight = False
                    f.hilight = True
                self.hilighted = f
                hit = True
                break
            offset_y += package_height

        if not hit:
            if self.hilighted:
                redraw.append(self.hilighted)
                self.hilighted.hilight = False
                self.hilighted = None

        if self.hilighted:
            if msg.command == yutani.MouseEvent.DOWN:
                if self.hilighted.do_action():
                    redraw = []
                    self.redraw_buf()
                    self.draw()

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

    try:
        msk.is_gui = True
        _manifest = msk.get_manifest()
        _local_index = msk.get_local_index()
        packages = []
    except:
        packages = [NoPackages()]

    window = PackageManagerWindow(d)
    if not packages:
        try:
            window.load_packages()
        except:
            window.packages = packages
    else:
        window.packages = packages
    window.redraw_buf()
    window.draw()

    yutani_mainloop.mainloop()
