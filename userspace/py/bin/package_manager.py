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
import toaru_package

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from icon_cache import get_icon

version = "0.1.0"
_description = f"<b>Package Manager {version}</b>\n© 2017 Kevin Lange\n\nBrowse and install software packages.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"

hilight_border_top = (54/255,128/255,205/255)
hilight_gradient_top = (93/255,163/255,236/255)
hilight_gradient_bottom = (56/255,137/255,220/55)
hilight_border_bottom = (47/255,106/255,167/255)

def install(name):
    toaru_package.process_package(name)
    toaru_package.calculate_upgrades()

    for package in toaru_package.packages_to_install:
        if package in toaru_package.upgrade_packages or package in toaru_package.install_packages:
            toaru_package.install_package(package)

    toaru_package.write_status()

    toaru_package.packages_to_install = []
    toaru_package.upgrade_packages = []
    toaru_package.install_packages = []

class Package(object):

    def __init__(self, name):
        self.name = name
        self.y = 0
        self.x = 0
        self.hilight = False

    @property
    def installed(self):
        return self.name in toaru_package.installed_packages

    @property
    def version(self):
        a,b,c= toaru_package.manifest['packages'][self.name]['version']
        return f"{a}.{b}.{c}"

    @property
    def text(self):
        check = "☐" if not self.installed else "☑"
        return f"{check} <b>{self.name}</b> - {self.version}"

    def do_action(self):
        if self.installed:
            return False
        install(self.name)
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
        super(PackageManagerWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title="Package Manager", icon="package", doublebuffer=True)
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
            subprocess.Popen(["about-applet.py","About Package Manager","package","/usr/share/icons/48/package.png",_description])
        def help_browser(action):
            subprocess.Popen(["help-browser.py","packages.trt"])
        menus = [
            ("File", [
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",help_browser,None),
                MenuEntryDivider(),
                MenuEntryAction("About Package Manager","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.menus = {}
        self.hovered_menu = None

        self.scroll_y = 0
        self.hilighted = None
        self.buf = None
        try:
            toaru_package.fetch_manifest()
            toaru_package.is_gui = True
            self.load_packages()
        except:
            self.packages = [NoPackages()]
        self.redraw_buf()
        self.hilighted = None

    def load_packages(self):
        self.packages = sorted([Package(name) for name in toaru_package.manifest['packages'].keys()],key=lambda x: x.name)

    def redraw_buf(self,clips=None):
        if self.buf:
            self.buf.destroy()
        w = self.width - self.decorator.width()
        self.buf = yutani.GraphicsBuffer(w,len(self.packages)*24)

        surface = self.buf.get_cairo_surface()
        ctx = cairo.Context(surface)

        if clips:
            for clip in clips:
                ctx.rectangle(clip.x,clip.y,w,24)
            ctx.clip()

        ctx.rectangle(0,0,surface.get_width(),surface.get_height())
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        offset_y = 0

        for f in self.packages:
            f.y = offset_y
            if not clips or f in clips:
                tr = text_region.TextRegion(4,offset_y+4,w-4,20)
                if f.hilight:
                    gradient = cairo.LinearGradient(0,0,0,18)
                    gradient.add_color_stop_rgba(0.0,*hilight_gradient_top,1.0)
                    gradient.add_color_stop_rgba(1.0,*hilight_gradient_bottom,1.0)
                    ctx.rectangle(0,offset_y+4,w,1)
                    ctx.set_source_rgb(*hilight_border_top)
                    ctx.fill()
                    ctx.rectangle(0,offset_y+4+20-1,w,1)
                    ctx.set_source_rgb(*hilight_border_bottom)
                    ctx.fill()
                    ctx.save()
                    ctx.translate(0,offset_y+4+1)
                    ctx.rectangle(0,0,w,20-2)
                    ctx.set_source(gradient)
                    ctx.fill()
                    ctx.restore()
                    tr.font.font_color = 0xFFFFFFFF
                else:
                    ctx.rectangle(0,offset_y+4,w,20)
                    ctx.set_source_rgb(1,1,1)
                    ctx.fill()
                    tr.font.font_color = 0xFF000000
                tr.set_richtext(f.text)
                tr.set_one_line()
                tr.set_ellipsis()
                tr.draw(self.buf)
            offset_y += 24

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
        rows = 1000
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

        if x < 0 or x >= w or y < 0 or y >= h:
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
                        MenuEntryAction("Up",None,self.go_up,None),
                    ]
                    menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)
                return

        if y < 0: return

        offset_y = self.scroll_y + self.menubar.height

        redraw = []
        hit = False

        for f in self.packages:
            if offset_y > h: break
            if y >= offset_y and y < offset_y + 24:
                if not f.hilight:
                    redraw.append(f)
                    if self.hilighted:
                        redraw.append(self.hilighted)
                        self.hilighted.hilight = False
                    f.hilight = True
                self.hilighted = f
                hit = True
                break
            offset_y += 24

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

    window = PackageManagerWindow(d)
    window.draw()

    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            window.close()
            break
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            if msg.wid == window.wid:
                window.keyboard_event(msg)
            elif msg.wid in window.menus:
                window.menus[msg.wid].keyboard_event(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            if msg.wid == window.wid:
                if msg.focused == 0 and window.menus:
                    window.focused = 1
                else:
                    window.focused = msg.focused
                window.draw()
            elif msg.wid in window.menus and msg.focused == 0:
                window.menus[msg.wid].leave_menu()
                if not window.menus and window.focused:
                    window.focused = 0
                    window.draw()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            if msg.wid == window.wid:
                window.finish_resize(msg)
        elif msg.type == yutani.Message.MSG_WINDOW_MOVE:
            if msg.wid == window.wid:
                window.x = msg.x
                window.y = msg.y
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            if msg.wid == window.wid:
                window.mouse_event(msg)
            elif msg.wid in window.menus:
                m = window.menus[msg.wid]
                if msg.new_x >= 0 and msg.new_x < m.width and msg.new_y >= 0 and msg.new_y < m.height:
                    window.hovered_menu = m
                elif window.hovered_menu == m:
                    window.hovered_menu = None
                m.mouse_action(msg)
