#!/usr/bin/python3
"""

Panel - Displays a window with various widgets at the top of the screen.

This panel is based on the original C implementation in ToaruOS, but is focused
on providing a more easily extended widget system. Each element of the panel is
a widget object which can independently draw and receive mouse events.

"""
import calendar
import configparser
import html
import math
import os
import signal
import sys
import shlex
import subprocess
import time

import cairo

import yutani
import text_region
import toaru_fonts
import fswait

from menu_bar import MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from icon_cache import get_icon

PANEL_HEIGHT=28

class BaseWidget(object):
    """Base class for a panel widget."""

    width = 0

    def draw(self, window, offset, remaining, ctx):
        pass

    def focus_enter(self):
        pass

    def focus_leave(self):
        pass

    def mouse_action(self, msg):
        return False

class FillWidget(BaseWidget):
    """Fills the panel with blank space. Only one such panel element should exist at a time."""

    width = -1

class CalendarMenuEntry(MenuEntryDivider):

    height = 130
    width = 200

    def __init__(self):
        self.font = toaru_fonts.Font(toaru_fonts.FONT_MONOSPACE,13,0xFF000000)
        self.tr = text_region.TextRegion(0,0,self.width-20,self.height,font=self.font)
        self.calendar = calendar.TextCalendar(calendar.SUNDAY)
        t = time.localtime(current_time)
        self.tr.set_line_height(17)
        self.tr.set_text(self.calendar.formatmonth(t.tm_year,t.tm_mon))
        for tu in self.tr.text_units:
            if tu.string == str(t.tm_mday):
                tu.set_font(toaru_fonts.Font(toaru_fonts.FONT_MONOSPACE_BOLD,13,0xFF000000))
                break

    def draw(self, window, offset, ctx):
        self.window = window
        self.tr.move(20,offset)
        self.tr.draw(window)

class ClockWidget(BaseWidget):
    """Displays a simple clock"""

    text_y_offset = 4
    width = 80
    color = 0xFFE6E6E6
    font_size = 16
    alignment = 0
    time_format = '<b>%H:%M:%S</b>'

    def __init__(self):
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, self.font_size, self.color)
        self.tr   = text_region.TextRegion(0,0,self.width,PANEL_HEIGHT-self.text_y_offset,font=self.font)
        self.tr.set_alignment(self.alignment)
        self.offset = 0

    def draw(self, window, offset, remaining, ctx):
        self.offset = offset
        self.window = window
        self.tr.move(offset,self.text_y_offset)
        self.tr.set_richtext(time.strftime(self.time_format,time.localtime(current_time)))
        self.tr.draw(window)

    def focus_enter(self):
        self.font.font_color = 0xFF8EDBFF

    def focus_leave(self):
        self.font.font_color = self.color

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            def _pass(action):
                pass
            menu_entries = [
                CalendarMenuEntry(),
            ]
            menu = MenuWindow(menu_entries,(self.offset-120,self.window.height),root=self.window)

class DateWidget(ClockWidget):
    """Displays the weekday and date on separate lines."""

    text_y_offset = 4
    color = 0xFFE6E6E6
    width = 70
    font_size = 9
    alignment = 2
    time_format = '%A\n<b>%B %e</b>'

class LogOutWidget(BaseWidget):
    """Simple button widget that ends the user session when clicked."""
    # TODO: Present a log out / restart menu instead.

    width = 28

    path = '/usr/share/icons/panel-shutdown.png'

    def __init__(self):
        self.icon = cairo.ImageSurface.create_from_png(self.path)
        self.icon_hilight = cairo.ImageSurface.create_from_png(self.path)
        tmp = cairo.Context(self.icon_hilight)
        tmp.set_operator(cairo.OPERATOR_ATOP)
        tmp.rectangle(0,0,24,24)
        tmp.set_source_rgb(0x8E/0xFF,0xD8/0xFF,1)
        tmp.paint()
        self.hilighted = False

    def draw(self, window, offset, remaining, ctx):
        self.offset = offset
        self.window = window
        if self.hilighted:
            ctx.set_source_surface(self.icon_hilight,offset+2,2)
        else:
            ctx.set_source_surface(self.icon,offset+2,2)
        ctx.paint()

    def focus_enter(self):
        self.hilighted = True

    def focus_leave(self):
        self.hilighted = False

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            yctx.session_end()
        return False

class RestartMenuWidget(LogOutWidget):

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            def exit(action):
                if 'callback' in dir(self):
                    self.callback()
                else:
                    sys.exit(0)
            menu_entries = [
                MenuEntryAction("Restart","exit",exit,None),
            ]
            menu = MenuWindow(menu_entries,(self.offset-120,self.window.height),root=self.window)
            menu.move(self.window.width - menu.width, self.window.height)
        return False

class LabelWidget(BaseWidget):
    """Provides a menu of applications to launch."""

    text_y_offset = 4
    text_x_offset = 10
    color = 0xFFE6E6E6
    hilight = 0xFF8ED8FF

    def __init__(self, text):
        self.width = 140
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD, 14, self.color)
        self.tr   = text_region.TextRegion(0,0,self.width-self.text_x_offset*2,PANEL_HEIGHT-self.text_y_offset,font=self.font)
        self.tr.set_text(text)

    def draw(self, window, offset, remaining, ctx):
        self.window = window
        self.tr.move(offset+self.text_x_offset,self.text_y_offset)
        self.tr.draw(window)

    def focus_enter(self):
        self.font.font_color = self.hilight

    def focus_leave(self):
        self.font.font_color = self.color

    def activate(self):
        pass # Extend this as needed

    def mouse_action(self,msg):
        if msg.command == yutani.MouseEvent.CLICK:
            self.activate()


class VolumeWidget(BaseWidget):
    """Volume control widget."""

    width = 28
    color = (0xE6/0xFF,0xE6/0xFF,0xE6/0xFF)
    hilight_color = (0x8E/0xFF,0xD8/0xFF,1)
    icon_names = ['volume-mute','volume-low','volume-medium','volume-full']
    check_time = 10

    def __init__(self):
        self.icons = {}
        self.icons_hilight = {}
        for name in self.icon_names:
            self.icons[name] = cairo.ImageSurface.create_from_png(f'/usr/share/icons/24/{name}.png')
            tmp = cairo.Context(self.icons[name])
            tmp.set_operator(cairo.OPERATOR_ATOP)
            tmp.rectangle(0,0,24,24)
            tmp.set_source_rgb(*self.color)
            tmp.paint()
            self.icons_hilight[name] = cairo.ImageSurface.create_from_png(f'/usr/share/icons/24/{name}.png')
            tmp = cairo.Context(self.icons_hilight[name])
            tmp.set_operator(cairo.OPERATOR_ATOP)
            tmp.rectangle(0,0,24,24)
            tmp.set_source_rgb(*self.hilight_color)
            tmp.paint()
        try:
            self.mixer_fd = open('/dev/mixer')
        except:
            self.mixer_fd = None
        self.volume = self.get_volume()
        self.muted = False
        self.previous_volume = 0
        self.hilighted = False
        self.last_check = 0

    def focus_enter(self):
        self.hilighted = True

    def focus_leave(self):
        self.hilighted = False

    def draw(self, window, offset, remaining, ctx):
        self.check()
        if self.volume < 10:
            source = 'volume-mute'
        elif self.volume < 0x547ae147:
            source = 'volume-low'
        elif self.volume < 0xa8f5c28e:
            source = 'volume-medium'
        else:
            source = 'volume-full'
        if self.hilighted:
            ctx.set_source_surface(self.icons_hilight[source],offset,2)
        else:
            ctx.set_source_surface(self.icons[source],offset,2)
        ctx.paint()

    def check(self):
        if current_time - self.last_check > self.check_time:
            self.last_check = current_time
            self.volume = self.get_volume()

    def get_volume(self):
        """Get the current mixer master volume."""
        import fcntl
        import struct
        knob = bytearray(struct.pack("III", 0, 0, 0)) # VOLUME_DEVICE_ID, VOLUME_KNOB_ID, <Unused>
        try:
            fcntl.ioctl(self.mixer_fd, 2, knob, True)
            _,_,value = struct.unpack("III", knob)
            return value
        except:
            return 0

    def set_volume(self):
        """Set the mixer master volume to the widget's volume level."""
        import fcntl
        import struct
        try:
            knob = struct.pack("III", 0, 0, self.volume) # VOLUME_DEVICE_ID, VOLUME_KNOB_ID, volume_level
            fcntl.ioctl(self.mixer_fd, 3, knob)
        except:
            pass

    def volume_up(self):
        self.volume += 0x8000000
        if self.volume >= 0x100000000:
            self.volume = 0xf8000000
        self.set_volume()

    def volume_down(self):
        self.volume -= 0x8000000
        if self.volume < 0:
            self.volume = 0
        self.set_volume()

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            if self.muted:
                self.muted = False
                self.volume = self.previous_volume
                self.set_volume()
            else:
                self.muted = True
                self.previous_volume = self.volume
                self.volume = 0
                self.set_volume()
            return True
        else:
            if msg.buttons & yutani.MouseButton.SCROLL_UP:
                self.volume_up()
                return True
            elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
                self.volume_down()
                return True

class NetworkWidget(BaseWidget):
    """Volume control widget."""

    width = 28
    color = (0xE6/0xFF,0xE6/0xFF,0xE6/0xFF)
    hilight_color = (0x8E/0xFF,0xD8/0xFF,1)
    icon_names = ['net-active','net-disconnected']
    check_time = 10

    def __init__(self):
        self.icons = {}
        self.icons_hilight = {}
        for name in self.icon_names:
            self.icons[name] = cairo.ImageSurface.create_from_png(f'/usr/share/icons/24/{name}.png')
            tmp = cairo.Context(self.icons[name])
            tmp.set_operator(cairo.OPERATOR_ATOP)
            tmp.rectangle(0,0,24,24)
            tmp.set_source_rgb(*self.color)
            tmp.paint()
            self.icons_hilight[name] = cairo.ImageSurface.create_from_png(f'/usr/share/icons/24/{name}.png')
            tmp = cairo.Context(self.icons_hilight[name])
            tmp.set_operator(cairo.OPERATOR_ATOP)
            tmp.rectangle(0,0,24,24)
            tmp.set_source_rgb(*self.hilight_color)
            tmp.paint()
        self.hilighted = False
        self.ip = None
        self.mac = None
        self.gw = None
        self.device = None
        self.dns = None
        self.last_check = 0
        self.status = 0

    def focus_enter(self):
        self.hilighted = True

    def focus_leave(self):
        self.hilighted = False

    def check(self):
        if current_time - self.last_check > self.check_time:
            self.last_check = current_time
            with open('/proc/netif','r') as f:
                lines = f.readlines()
                if len(lines) < 4 or "no network" in lines[0]:
                    self.status = 0
                else:
                    self.status = 1
                    _,self.ip = lines[0].strip().split('\t')
                    _,self.mac = lines[1].strip().split('\t')
                    _,self.device = lines[2].strip().split('\t')
                    _,self.dns = lines[3].strip().split('\t')
                    _,self.gw = lines[4].strip().split('\t')

    def draw(self, window, offset, remaining, ctx):
        self.check()
        self.offset = offset
        self.window = window
        if self.status == 1:
            source = 'net-active'
        else:
            source = 'net-disconnected'
        if self.hilighted:
            ctx.set_source_surface(self.icons_hilight[source],offset,2)
        else:
            ctx.set_source_surface(self.icons[source],offset,2)
        ctx.paint()

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            def _pass(action):
                pass
            if self.status == 1:
                menu_entries = [
                    MenuEntryAction(f"IP: {self.ip}",None,_pass,None),
                    MenuEntryAction(f"Primary DNS: {self.dns}",None,_pass,None),
                    MenuEntryAction(f"Gateway: {self.gw}",None,_pass,None),
                    MenuEntryAction(f"MAC: {self.mac}",None,_pass,None),
                    MenuEntryAction(f"Device: {self.device}",None,_pass,None),
                ]
            else:
                menu_entries = [
                    MenuEntryAction(f"No network.",None,_pass,None),
                ]
            menu = MenuWindow(menu_entries,(self.offset-100,PANEL_HEIGHT),root=self.window)

class WindowListWidget(FillWidget):
    """Displays a list of windows with icons and titles."""

    text_y_offset = 5
    color = 0xFFE6E6E6
    hilight = 0xFF8ED8FF
    icon_width = 48

    def __init__(self):
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, self.color)
        self.font.set_shadow((0xFF000000, 2, 1, 1, 3.0))
        self.font_hilight = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, self.hilight)
        self.font_hilight.set_shadow((0xFF000000, 2, 1, 1, 3.0))
        self.gradient = cairo.LinearGradient(0,0,0,PANEL_HEIGHT)
        self.gradient.add_color_stop_rgba(0.0,72/255,167/255,255/255,0.7)
        self.gradient.add_color_stop_rgba(1.0,72/255,167/255,255/255,0.0)

        self.divider = cairo.LinearGradient(0,0,0,PANEL_HEIGHT)
        self.divider.add_color_stop_rgba(0.1,1,1,1,0.0)
        self.divider.add_color_stop_rgba(0.5,1,1,1,1.0)
        self.divider.add_color_stop_rgba(0.9,1,1,1,0.0)
        self.hovered = None
        self.unit_width = None
        self.offset = 0

    def draw(self, window, offset, remaining, ctx):
        global windows
        if not len(windows):
            return
        self.window = window
        self.offset = offset
        available_width = remaining - offset
        self.unit_width = min(int(available_width / len(windows)),180)
        icon_width = self.icon_width
        if self.unit_width < 56:
            self.unit_width = 32
            icon_width = 24

        i = 0 
        for w in windows:
            if w.flags & 1:
                ctx.set_source(self.gradient)
                ctx.rectangle(offset+4,0,self.unit_width-4,PANEL_HEIGHT)
                ctx.fill()
            icon = get_icon(w.icon, icon_width)

            ctx.save()
            ctx.translate(offset + self.unit_width - icon_width - 2,0)
            ctx.rectangle(0,0,icon_width + 4,PANEL_HEIGHT-2)
            ctx.clip()
            if icon.get_width() != icon_width:
                ctx.scale(icon_width/icon.get_width(),icon_width/icon.get_width())
            ctx.set_source_surface(icon,0,0)
            if self.unit_width < 48:
                ctx.paint()
            else:
                ctx.paint_with_alpha(0.7)
            if i < len(windows) - 1:
                ctx.rectangle(icon_width + 3,0,1,PANEL_HEIGHT)
                ctx.set_source(self.divider)
                ctx.fill()
            ctx.restore()

            offset_left = 4
            if self.unit_width > 48:
                font = self.font
                if self.hovered == w.wid:
                    font = self.font_hilight
                tr = text_region.TextRegion(offset+4+offset_left,self.text_y_offset,self.unit_width - 6 - offset_left,PANEL_HEIGHT-self.text_y_offset,font=font)
                tr.set_one_line()
                tr.set_ellipsis()
                tr.set_text(w.name)
                tr.draw(window)


            offset += self.unit_width
            i += 1

    def focus_leave(self):
        self.hovered = None

    def mouse_action(self, msg):
        if not len(windows):
            return
        msg.new_x -= self.offset
        hovered_index = int(msg.new_x / self.unit_width)
        previously_hovered = self.hovered
        if hovered_index < len(windows):
            self.hovered = windows[hovered_index].wid
            if msg.command == yutani.MouseEvent.CLICK:
                yctx.focus_window(self.hovered)
            elif msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
                if not self.window.menus:
                    def move_window(window):
                        yutani.yutani_lib.yutani_window_drag_start_wid(yutani.yutani_ctx._ptr,window)
                    #def close_window(window):
                    #    print("Should close window",window)
                    menu_entries = [
                        MenuEntryAction("Move",None,move_window,self.hovered),
                        #MenuEntryAction("Close",None,close_window,self.hovered)
                    ]
                    menu = MenuWindow(menu_entries,(msg.new_x+self.offset,PANEL_HEIGHT),root=self.window)
        else:
            self.hovered = None
        return self.hovered != previously_hovered


class ApplicationsMenuWidget(BaseWidget):
    """Provides a menu of applications to launch."""

    text_y_offset = 4
    text_x_offset = 10
    color = 0xFFE6E6E6
    hilight = 0xFF8ED8FF

    def __init__(self):
        self.width = 140
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD, 14, self.color)
        self.tr   = text_region.TextRegion(0,0,self.width-self.text_x_offset*2,PANEL_HEIGHT-self.text_y_offset,font=self.font)
        self.tr.set_text("Applications")

        self.reinit_menus()

    def extra(self, name):
        if not os.path.exists(f'/usr/share/menus/{name}'):
            return []
        else:
            out = []
            for icon in os.listdir(f'/usr/share/menus/{name}'):
                with open(f'/usr/share/menus/{name}/{icon}','r') as f:
                    icon,command,title = f.read().strip().split(',')
                    out.append(MenuEntryAction(title,icon,launch_app,command))
            return out

    def reinit_menus(self):
        accessories = [
            MenuEntryAction("Calculator","calculator",launch_app,"calculator.py"),
            MenuEntryAction("Clock Widget","clock",launch_app,"clock.py"),
            MenuEntryAction("File Browser","folder",launch_app,"file_browser.py"),
            MenuEntryAction("Terminal","utilities-terminal",launch_app,"terminal"),
        ]
        accessories.extend(self.extra('accessories'))
        demos = [
            MenuEntrySubmenu("Cairo",[
                MenuEntryAction("Cairo Demo","cairo-demo",launch_app,"cairo-demo"),
                MenuEntryAction("Cairo Snow","snow",launch_app,"make-it-snow"),
                MenuEntryAction("Pixman Demo","pixman-demo",launch_app,"pixman-demo"),
            ]),
            MenuEntrySubmenu("Mesa (swrast)",[
                MenuEntryAction("Gears","gears",launch_app,"gears"),
                MenuEntryAction("Teapot","teapot",launch_app,"teapot"),
            ]),
            MenuEntryAction("Draw Lines","drawlines",launch_app,"drawlines"),
            MenuEntryAction("Julia Fractals","julia",launch_app,"julia"),
            MenuEntryAction("Plasma","plasma",launch_app,"plasma"),
        ]
        demos.extend(self.extra('demos'))
        games = [
            MenuEntryAction("Mines","mines",launch_app,"mines.py"),
        ]
        games.extend(self.extra('games'))
        graphics = [
            MenuEntryAction("ToaruPaint","applications-painting",launch_app,"painting.py"),
        ]
        graphics.extend(self.extra('graphics'))
        settings = [
            MenuEntryAction("Package Manager","package",launch_app,"gsudo package_manager.py"),
            MenuEntryAction("Select Wallpaper","select-wallpaper",launch_app,"select_wallpaper.py"),
        ]
        settings.extend(self.extra('settings'))

        self.menu_entries = [
            MenuEntrySubmenu("Accessories",accessories),
            MenuEntrySubmenu("Demos",demos),
            MenuEntrySubmenu("Games",games),
            MenuEntrySubmenu("Graphics",graphics),
            MenuEntrySubmenu("Settings",settings),
            MenuEntryDivider(),
            MenuEntryAction("Help","help",launch_app,"help-browser.py"),
            MenuEntryAction("About ToaruOS","star",launch_app,"about-applet.py"),
            MenuEntryAction("Log Out","exit",logout_callback,""),
        ]

    def draw(self, window, offset, remaining, ctx):
        self.window = window
        self.tr.move(offset+self.text_x_offset,self.text_y_offset)
        self.tr.draw(window)

    def focus_enter(self):
        self.font.font_color = self.hilight

    def focus_leave(self):
        self.font.font_color = self.color

    def activate(self):
        menu = MenuWindow(self.menu_entries,(0,self.window.height),root=self.window)

    def mouse_action(self,msg):
        if msg.command == yutani.MouseEvent.CLICK:
            self.activate()

class PanelWindow(yutani.Window):
    """The panel itself."""

    def __init__(self, widgets):
        self.widgets = widgets
        flags = yutani.WindowFlag.FLAG_NO_STEAL_FOCUS | yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE
        super(PanelWindow, self).__init__(yutani.yutani_ctx._ptr.contents.display_width,PANEL_HEIGHT,doublebuffer=True,flags=flags)
        self.move(0,0)
        self.set_stack(yutani.WindowStackOrder.ZORDER_TOP)
        self.focused_widget = None

        self.menus = {}
        self.hovered_menu = None

        # Panel background
        self.background = cairo.ImageSurface.create_from_png('/usr/share/panel.png')
        self.background_pattern = cairo.SurfacePattern(self.background)
        self.background_pattern.set_extend(cairo.EXTEND_REPEAT)

        self.visible = True

    def toggle_visibility(self):
        if not self.visible:
            for i in range(PANEL_HEIGHT-1,-1,-1):
                self.move(0,-i)
                yutani.usleep(10000)
            self.visible = True
        else:
            for i in range(1,PANEL_HEIGHT,1):
                self.move(0,-i)
                yutani.usleep(10000)
            self.visible = False

    def draw(self):
        """Draw the window."""
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.set_source(self.background_pattern)
        ctx.paint()
        ctx.set_operator(cairo.OPERATOR_OVER)

        offset = 0
        index = 0
        for widget in self.widgets:
            index += 1
            remaining = 0 if widget.width != -1 else self.width - sum([widget.width for widget in self.widgets[index:]])
            widget.draw(self, offset, remaining, ctx)
            offset = offset + widget.width if widget.width != -1 else remaining

        self.flip()

    def finish_resize(self, msg):
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()


    def mouse_event(self, msg):
        redraw = False
        if (msg.command == 5 or msg.new_y >= self.height) and self.focused_widget:
            self.focused_widget.focus_leave()
            self.focused_widget = None
            redraw = True
        elif msg.new_y < self.height:
            widget_under_mouse = None
            offset = 0
            index = 0
            for widget in self.widgets:
                index += 1
                remaining = 0 if widget.width != -1 else self.width - sum([widget.width for widget in self.widgets[index:]])
                if msg.new_x >= offset and msg.new_x < (offset + widget.width if widget.width != -1 else remaining):
                    widget_under_mouse = widget
                    break
                offset = offset + widget.width if widget.width != -1 else remaining
            if widget_under_mouse != self.focused_widget:
                if self.focused_widget:
                    self.focused_widget.focus_leave()
                    redraw = True
                self.focused_widget = widget_under_mouse
                if self.focused_widget:
                    self.focused_widget.focus_enter()
                    redraw = True
            elif widget_under_mouse:
                if widget_under_mouse.mouse_action(msg):
                    self.draw()
        if redraw:
            self.draw()


class WallpaperIcon(object):

    icon_width = 48
    width = 100
    height = 80

    def __init__(self, icon, name, action, data):
        self.name = name
        self.action = action
        self.data = data

        self.x = 0
        self.y = 0

        self.hilighted = False
        self.icon = get_icon(icon,self.icon_width)
        self.icon_hilight = cairo.ImageSurface(self.icon.get_format(),self.icon.get_width(),self.icon.get_height())
        tmp = cairo.Context(self.icon_hilight)
        tmp.set_operator(cairo.OPERATOR_SOURCE)
        tmp.set_source_surface(self.icon,0,0)
        tmp.paint()
        tmp.set_operator(cairo.OPERATOR_ATOP)
        tmp.rectangle(0,0,self.icon_hilight.get_width(),self.icon_hilight.get_height())
        tmp.set_source_rgba(0x8E/0xFF,0xD8/0xFF,1,0.3)
        tmp.paint()

        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFFFFFFFF)
        self.font.set_shadow((0xFF000000, 2, 1, 1, 3.0))
        self.tr = text_region.TextRegion(0,0,self.width,15,font=self.font)
        self.tr.set_alignment(2)
        self.tr.set_text(self.name)
        self.tr.set_one_line()

    def draw(self, window, offset, ctx, animating=False):
        x, y = offset

        self.x = x
        self.y = y

        self.tr.move(x,y+self.icon_width+5)
        self.tr.draw(window)

        left_pad = int((self.width - self.icon_width)/2)

        icon = self.icon_hilight if self.hilighted else self.icon

        ctx.save()
        ctx.translate(x+left_pad,y)
        if icon.get_width() != self.icon_width:
            ctx.scale(self.icon_width/icon.get_width(),self.icon_width/icon.get_width())
        ctx.set_source_surface(icon,0,0)
        ctx.paint()
        ctx.restore()

        if animating and animating < 0.5:
            ctx.save()
            ctx.translate(x+left_pad,y)
            if icon.get_width() != self.icon_width:
                ctx.scale(self.icon_width/icon.get_width(),self.icon_width/icon.get_width())
            scale = 1.0 + animating/0.8
            n = (self.icon_width - self.icon_width * scale) / 2
            ctx.translate(n,0)
            ctx.scale(scale,scale)
            ctx.set_source_surface(icon,0,0)
            ctx.paint_with_alpha(1.0 - animating/0.5)
            ctx.restore()

    def focus_enter(self):
        self.hilighted = True

    def focus_leave(self):
        self.hilighted = False

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            self.action(self)

class WallpaperWindow(yutani.Window):
    """Manages the desktop wallpaper window."""

    fallback = '/usr/share/wallpapers/default'

    def __init__(self):
        w = yutani.yutani_ctx._ptr.contents.display_width
        h = yutani.yutani_ctx._ptr.contents.display_height
        flags = yutani.WindowFlag.FLAG_NO_STEAL_FOCUS | yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE
        super(WallpaperWindow, self).__init__(w,h,doublebuffer=True,flags=flags)
        self.move(0,0)
        self.set_stack(yutani.WindowStackOrder.ZORDER_BOTTOM)

        # TODO get the user's selected wallpaper
        self.background = self.load_wallpaper()
        self.icons = self.load_icons()
        self.focused_icon = None
        self.animations = {}
        self.x = 0 # For clipping
        self.y = 0

    def animate_new(self):
        self.new_background = self.load_wallpaper()
        self.animations[self] = time.time()

    def add_animation(self, icon):
        self.animations[icon] = time.time()

    def animate(self):
        tick = time.time()
        self.draw(self.animations.keys())
        ditch = []
        for icon in self.animations:
            if icon == self:
                continue
            if tick - self.animations[icon] > 0.5:
                ditch.append(icon)
        for icon in ditch:
            del self.animations[icon]


    def load_icons(self):
        home = os.environ['HOME']
        path = f'{home}/.desktop'
        if not os.path.exists(path):
            path = '/etc/default.desktop'
        icons = []
        with open(path) as f:
            for line in f:
                icons.append(line.strip().split(','))

        wallpaper = self
        def launch_application(self):
            wallpaper.add_animation(self)
            launch_app(self.data)

        out = []
        for icon in icons:
            out.append(WallpaperIcon(icon[0],icon[2],launch_application,icon[1]))

        return out

    def load_wallpaper(self, path=None):
        if not path:
            home = os.environ['HOME']
            conf = f'{home}/.desktop.conf'
            if not os.path.exists(conf):
                path = self.fallback
            else:
                with open(conf,'r') as f:
                    conf_str = '[desktop]\n' + f.read()
                c = configparser.ConfigParser()
                c.read_string(conf_str)
                path = c['desktop'].get('wallpaper',self.fallback)
        return cairo.ImageSurface.create_from_png(path)

    def finish_resize(self, msg):
        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.draw()
        self.resize_done()
        self.flip()

    def draw(self, clips=None):
        """Draw the window."""
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        ctx.save()

        if clips:
            for clip in clips:
                ctx.rectangle(clip.x,clip.y,clip.width,clip.height)
            if self.animations:
                for clip in self.animations:
                    ctx.rectangle(clip.x,clip.y,clip.width,clip.height)
            ctx.clip()
        ctx.set_operator(cairo.OPERATOR_SOURCE)

        x = self.width / self.background.get_width()
        y = self.height / self.background.get_height()

        nh = int(x * self.background.get_height())
        nw = int(y * self.background.get_width())

        if (nw > self.width):
            ctx.translate((self.width - nw) / 2, 0)
            ctx.scale(y,y)
        else:
            ctx.translate(0,(self.height - nh) / 2)
            ctx.scale(x,x)

        ctx.set_source_surface(self.background,0,0)
        ctx.paint()
        ctx.restore()


        ctx.set_operator(cairo.OPERATOR_OVER)
        clear_animation = False

        if self in self.animations:
            ctx.save()
            x = self.width / self.new_background.get_width()
            y = self.height / self.new_background.get_height()

            nh = int(x * self.new_background.get_height())
            nw = int(y * self.new_background.get_width())

            if (nw > self.width):
                ctx.translate((self.width - nw) / 2, 0)
                ctx.scale(y,y)
            else:
                ctx.translate(0,(self.height - nh) / 2)
                ctx.scale(x,x)

            ctx.set_source_surface(self.new_background,0,0)
            diff = time.time()-self.animations[self]
            if diff >= 1.0:
                self.background = self.new_background
                clear_animation = True
                ctx.paint()
            else:
                ctx.paint_with_alpha(diff/1.0)

            ctx.restore()

        offset_x = 20
        offset_y = 50
        last_width = 0
        for icon in self.icons:
            if offset_y > self.height - icon.height:
                offset_y = 50
                offset_x += last_width
                last_width = 0
            if icon.width > last_width:
                last_width = icon.width
            if not clips or icon in clips or icon in self.animations or self in self.animations:
                icon.draw(self,(offset_x,offset_y),ctx,time.time()-self.animations[icon] if icon in self.animations else False)
            offset_y += icon.height

        if clear_animation:
            del self.animations[self]

        self.flip()

    def mouse_event(self, msg):
        redraw = False
        clips = []
        if (msg.command == 5 or msg.new_y >= self.height) and self.focused_icon:
            self.focused_icon.focus_leave()
            clips.append(self.focused_icon)
            self.focused_icon = None
            redraw = True
        else:
            icon_under_mouse = None
            offset_x = 20
            offset_y = 50
            last_width = 0
            for icon in self.icons:
                if offset_y > self.height - icon.height:
                    offset_y = 50
                    offset_x += last_width
                    last_width = 0
                if icon.width > last_width:
                    last_width = icon.width
                if msg.new_x >= offset_x and msg.new_x < offset_x + icon.width and msg.new_y >= offset_y and msg.new_y < offset_y + icon.height:
                    icon_under_mouse = icon
                    break
                offset_y += icon.height
            if icon_under_mouse != self.focused_icon:
                if self.focused_icon:
                    self.focused_icon.focus_leave()
                    redraw = True
                    clips.append(self.focused_icon)
                self.focused_icon = icon_under_mouse
                if self.focused_icon:
                    self.focused_icon.focus_enter()
                    redraw = True
                    clips.append(self.focused_icon)
            elif icon_under_mouse:
                if icon_under_mouse.mouse_action(msg):
                    self.draw()
                    clips.append(icon_under_mouse)
        if redraw:
            self.draw(clips)

class AlttabWindow(yutani.Window):
    """Displays the currently selected window for Alt-Tab switching."""

    icon_width = 48
    color = 0xFFE6E6E6

    def __init__(self):
        flags = yutani.WindowFlag.FLAG_NO_STEAL_FOCUS | yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE
        super(AlttabWindow,self).__init__(300,115,doublebuffer=True,flags=flags)
        w = yutani.yutani_ctx._ptr.contents.display_width
        h = yutani.yutani_ctx._ptr.contents.display_height
        self.move(int((w-self.width)/2),int((h-self.height)/2))
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD, 14, self.color)

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_source_rgba(0,0,0,0)
        ctx.fill()

        ctx.set_operator(cairo.OPERATOR_OVER)
        rounded_rectangle(ctx,0,0,self.width,self.height,10)
        ctx.set_source_rgba(0,0,0,0.7)
        ctx.fill()

        if new_focused >= 0 and new_focused < len(windows_zorder):
            w = windows_zorder[new_focused]

            icon = get_icon(w.icon,self.icon_width)

            ctx.save()
            ctx.translate(int((self.width-self.icon_width)/2),20)
            if icon.get_width() != self.icon_width:
                ctx.scale(self.icon_width/icon.get_width(),self.icon_width/icon.get_width())
            ctx.set_source_surface(icon,0,0)
            ctx.paint()
            ctx.restore()

            font = self.font
            tr = text_region.TextRegion(0,70,self.width,30,font=font)
            tr.set_one_line()
            tr.set_ellipsis()
            tr.set_alignment(2)
            tr.set_text(w.name)
            tr.draw(self)


        self.flip()

class ApplicationRunnerWindow(yutani.Window):
    """Displays the currently selected window for Alt-Tab switching."""

    icon_width = 48
    color = 0xFFE6E6E6

    def __init__(self):
        super(ApplicationRunnerWindow,self).__init__(400,115,doublebuffer=True)
        w = yutani.yutani_ctx._ptr.contents.display_width
        h = yutani.yutani_ctx._ptr.contents.display_height
        self.move(int((w-self.width)/2),int((h-self.height)/2))
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD, 16, self.color)
        self.data = ""
        self.complete = ""
        self.completed = False
        self.bins = []
        for d in os.environ.get("PATH").split(":"):
            if os.path.exists(d):
                self.bins.extend(os.listdir(d))

    def close(self):
        global app_runner
        app_runner = None
        super(ApplicationRunnerWindow,self).close()

    def try_complete(self):
        if not self.data:
            self.complete = ""
            self.completed = False
            return
        for b in sorted(self.bins):
            if b.startswith(self.data):
                self.complete = b[len(self.data):]
                self.completed = True
                return
        self.completed = False
        self.complete = ""

    def key_action(self, msg):
        if not msg.event.action == yutani.KeyAction.ACTION_DOWN:
            return
        if msg.event.keycode == yutani.Keycode.ESCAPE:
            self.close()
            return
        if msg.event.keycode == yutani.Keycode.DEL:
            self.complete = ""
            self.completed = False
            self.draw()
            return
        if msg.event.key == b'\x00':
            return
        if msg.event.key == b'\n':
            if self.data:
                launch_app(self.data + self.complete, terminal=bool(msg.event.modifiers & yutani.Modifier.MOD_LEFT_SHIFT))
            self.close()
            return
        if msg.event.key == b'\b':
            if self.data:
                self.data = self.data[:-1]
                self.try_complete()
        else:
            self.data += msg.event.key.decode('utf-8')
            self.try_complete()
        self.draw()

    def match_icon(self):
        icons = {
            "calculator.py": "calculator",
            "clock-win": "clock",
            "file_browser.py": "file-browser",
            "make-it-snow": "snow",
            "game": "applications-simulation",
            "draw": "applications-painting",
            "about-applet.py": "star",
            "help-browser.py": "help",
            "terminal": "utilities-terminal",
        }
        x = (self.data+self.complete).split(" ")[0]
        if x in icons:
            return get_icon(icons[x],self.icon_width) # Odd names
        elif self.completed:
            return get_icon(x,self.icon_width) # Fallback
        return None

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_source_rgba(0,0,0,0)
        ctx.fill()

        ctx.set_operator(cairo.OPERATOR_OVER)
        rounded_rectangle(ctx,0,0,self.width,self.height,10)
        ctx.set_source_rgba(0,0,0,0.7)
        ctx.fill()

        icon = self.match_icon()
        if icon:

            ctx.save()
            ctx.translate(20,20)
            if icon.get_width() != self.icon_width:
                ctx.scale(self.icon_width/icon.get_width(),self.icon_width/icon.get_width())
            ctx.set_source_surface(icon,0,0)
            ctx.paint()
            ctx.restore()

        font = self.font
        tr = text_region.TextRegion(0,20,self.width,30,font=font)
        tr.set_one_line()
        tr.set_ellipsis()
        tr.set_alignment(2)
        tr.set_richtext(html.escape(self.data) + '<color 0x888888>' + html.escape(self.complete) + '</color>')
        tr.draw(self)


        self.flip()


def rounded_rectangle(ctx,x,y,w,h,r):
    degrees = math.pi / 180
    ctx.new_sub_path()

    ctx.arc(x + w - r, y + r, r, -90 * degrees, 0 * degrees)
    ctx.arc(x + w - r, y + h - r, r, 0 * degrees, 90 * degrees)
    ctx.arc(x + r, y + h - r, r, 90 * degrees, 180 * degrees)
    ctx.arc(x + r, y + r, r, 180 * degrees, 270 * degrees)
    ctx.close_path()

def launch_app(item,terminal=False):
    """Launch an application in the background."""
    if terminal:
        subprocess.Popen(['/bin/terminal',item])
    else:
        subprocess.Popen(shlex.split(item))

def logout_callback(item):
    """Request the active session be stopped."""
    yctx.session_end()

def finish_alt_tab(msg):
    """When Alt is released, call this to close the alt-tab window and focus the requested window."""
    global tabbing, new_focused, alttab

    w = windows_zorder[new_focused]
    yctx.focus_window(w.wid)
    tabbing = False
    new_focused = -1

    alttab.close()

def reload_wallpaper(signum, frame):
    """Respond to SIGUSR1 by reloading the wallpaper."""
    wallpaper.animate_new()
    appmenu.reinit_menus()

def alt_tab(msg):
    """When Alt+Tab or Alt+Shift+Tab are pressed, call this to set the active alt-tab window."""
    global tabbing, new_focused, alttab
    direction = 1 if (msg.event.modifiers & yutani.Modifier.MOD_LEFT_SHIFT) else -1
    if len(windows_zorder) < 1:
        return

    if tabbing:
        new_focused = new_focused + direction
    else:
        new_focused = len(windows_zorder) - 1 + direction
        alttab = AlttabWindow()

    if new_focused < 0:
        new_focused = len(windows_zorder)-1
    elif new_focused >= len(windows_zorder):
        new_focused = 0

    tabbing = True
    alttab.draw()

def set_binds():

    # Show terminal
    yctx.key_bind(ord('t'), yutani.Modifier.MOD_LEFT_CTRL | yutani.Modifier.MOD_LEFT_ALT, yutani.KeybindFlag.BIND_STEAL)

    # Application runner
    yctx.key_bind(yutani.Keycode.F2, yutani.Modifier.MOD_LEFT_ALT, yutani.KeybindFlag.BIND_STEAL)

    # Menu
    yctx.key_bind(yutani.Keycode.F1, yutani.Modifier.MOD_LEFT_ALT, yutani.KeybindFlag.BIND_STEAL)

    # Hide/show panel
    yctx.key_bind(yutani.Keycode.F11, yutani.Modifier.MOD_LEFT_CTRL, yutani.KeybindFlag.BIND_STEAL)

    # Alt-tab forward and backward
    yctx.key_bind(ord("\t"), yutani.Modifier.MOD_LEFT_ALT, yutani.KeybindFlag.BIND_STEAL)
    yctx.key_bind(ord("\t"), yutani.Modifier.MOD_LEFT_ALT | yutani.Modifier.MOD_LEFT_SHIFT, yutani.KeybindFlag.BIND_STEAL)
    # Release alt
    yctx.key_bind(yutani.Keycode.LEFT_ALT, 0, yutani.KeybindFlag.BIND_PASSTHROUGH)

def reset_zorder(signum, frame):
    wallpaper.set_stack(yutani.WindowStackOrder.ZORDER_BOTTOM)
    panel.set_stack(yutani.WindowStackOrder.ZORDER_TOP)
    set_binds()

def maybe_animate():
    global current_time
    tick = int(time.time())
    if tick != current_time:
        try:
            os.waitpid(-1,os.WNOHANG)
        except ChildProcessError:
            pass
        current_time = tick
        panel.draw()
    if wallpaper.animations:
        wallpaper.animate()

if __name__ == '__main__':
    yctx = yutani.Yutani()

    appmenu = ApplicationsMenuWidget()
    widgets = [appmenu,WindowListWidget(),VolumeWidget(),NetworkWidget(),DateWidget(),ClockWidget(),LogOutWidget()]
    panel = PanelWindow(widgets)

    wallpaper = WallpaperWindow()
    wallpaper.draw()

    app_runner = None

    # Tabbing
    tabbing = False
    alttab = None
    new_focused = -1

    yctx.subscribe()

    set_binds()

    def update_window_list():
        yctx.query_windows()

        while 1:
            ad = yctx.wait_for(yutani.Message.MSG_WINDOW_ADVERTISE)
            if ad.size == 0:
                return

            yield ad


    windows_zorder = [x for x in update_window_list()]
    windows = sorted(windows_zorder, key=lambda window: window.wid)

    current_time = int(time.time())

    panel.draw()

    signal.signal(signal.SIGUSR1, reload_wallpaper)
    with open('/tmp/.wallpaper.pid','w') as f:
        f.write(str(os.getpid())+'\n')

    signal.signal(signal.SIGUSR2, reset_zorder)

    fds = [yutani.yutani_ctx]
    while 1:
        # Poll for events.
        fd = fswait.fswait(fds,500 if not wallpaper.animations else 20)
        maybe_animate()
        while yutani.yutani_ctx.query():
            msg = yutani.yutani_ctx.poll()
            if msg.type == yutani.Message.MSG_SESSION_END:
                # All applications should attempt to exit on SESSION_END.
                panel.close()
                wallpaper.close()
                msg.free()
                break
            elif msg.type == yutani.Message.MSG_NOTIFY:
                # Update the window list.
                windows_zorder = [x for x in update_window_list()]
                windows = sorted(windows_zorder, key=lambda window: window.wid)
                panel.draw()
            elif msg.type == yutani.Message.MSG_KEY_EVENT:
                if app_runner and msg.wid == app_runner.wid:
                    app_runner.key_action(msg)
                    msg.free()
                    continue
                if not app_runner and \
                    (msg.event.modifiers & yutani.Modifier.MOD_LEFT_ALT) and \
                    (msg.event.keycode == yutani.Keycode.F2) and \
                    (msg.event.action == yutani.KeyAction.ACTION_DOWN):
                    app_runner = ApplicationRunnerWindow()
                    app_runner.draw()
                if not panel.menus and \
                    (msg.event.modifiers & yutani.Modifier.MOD_LEFT_ALT) and \
                    (msg.event.keycode == yutani.Keycode.F1) and \
                    (msg.event.action == yutani.KeyAction.ACTION_DOWN):
                    appmenu.activate()
                # Ctrl-Alt-T: Open Terminal
                if (msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL) and \
                    (msg.event.modifiers & yutani.Modifier.MOD_LEFT_ALT) and \
                    (msg.event.keycode == ord('t')) and \
                    (msg.event.action == yutani.KeyAction.ACTION_DOWN):
                    launch_app('terminal')
                # Ctrl-F11: Toggle visibility of panel
                if (msg.event.modifiers & yutani.Modifier.MOD_LEFT_CTRL) and \
                    (msg.event.keycode == yutani.Keycode.F11) and \
                    (msg.event.action == yutani.KeyAction.ACTION_DOWN):
                    panel.toggle_visibility()
                # Release alt while alt-tabbing
                if tabbing and (msg.event.keycode == 0 or msg.event.keycode == yutani.Keycode.LEFT_ALT) and \
                    (msg.event.modifiers == 0) and (msg.event.action == yutani.KeyAction.ACTION_UP):
                    finish_alt_tab(msg)
                # Alt-Tab and Alt-Shift-Tab: Switch window focus.
                if (msg.event.modifiers & yutani.Modifier.MOD_LEFT_ALT) and \
                    (msg.event.keycode == ord("\t")) and \
                    (msg.event.action == yutani.KeyAction.ACTION_DOWN):
                    alt_tab(msg)
                if msg.wid in panel.menus:
                    panel.menus[msg.wid].keyboard_event(msg)
            elif msg.type == yutani.Message.MSG_WELCOME:
                # Display size has changed.
                panel.resize(msg.display_width, PANEL_HEIGHT)
                wallpaper.resize(msg.display_width, msg.display_height)
            elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
                # Resize the window.
                if msg.wid == panel.wid:
                    panel.finish_resize(msg)
                elif msg.wid == wallpaper.wid:
                    wallpaper.finish_resize(msg)
            elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
                if msg.wid == panel.wid:
                    panel.mouse_event(msg)
                elif msg.wid == wallpaper.wid:
                    wallpaper.mouse_event(msg)
                if msg.wid in panel.menus:
                    m = panel.menus[msg.wid]
                    if msg.new_x >= 0 and msg.new_x < m.width and msg.new_y >= 0 and msg.new_y < m.height:
                        panel.hovered_menu = m
                    elif panel.hovered_menu == m:
                        panel.hovered_menu = None
                    m.mouse_action(msg)
            elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
                if msg.wid in panel.menus and msg.focused == 0:
                    panel.menus[msg.wid].leave_menu()
            msg.free()

