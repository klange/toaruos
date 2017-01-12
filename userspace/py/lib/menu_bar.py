"""
Provides basic nested menus.
"""
import cairo

import yutani
import text_region
import toaru_fonts
from icon_cache import get_icon

class MenuBarWidget(object):
    """Widget for display multiple menus."""

    height = 24

    def __init__(self, window, entries):
        self.window = window
        self.entries = entries
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,13,0xFFFFFFFF)

    def draw(self, cr, x, y, width):
        self.x = x
        self.y = y
        self.width = width

        cr.save()
        cr.set_source_rgb(59/255,59/255,59/255)
        cr.rectangle(0,0,width,self.height)
        cr.fill()
        cr.restore()

        x_, y_ = cr.user_to_device(x,y)
        offset = 0
        for e in self.entries:
            title, _ = e
            w = self.font.width(title) + 10
            tr = text_region.TextRegion(int(x_)+8+offset,int(y_)+2,w,self.height,self.font)
            tr.set_one_line()
            tr.set_text(title)
            tr.draw(self.window)
            offset += w

    def mouse_event(self, msg, x, y):
        offset = 0
        for e in self.entries:
            title, menu = e
            w = self.font.width(title) + 10
            if x >= offset and x < offset + w:
                if msg.command == yutani.MouseEvent.CLICK: # or raise
                    menu = MenuWindow(menu,(self.window.x+self.window.decorator.left_width()+offset,self.window.y+self.window.decorator.top_height()+self.height),root=self.window)
                    break
            offset += w


class MenuEntryAction(object):
    """Menu entry class for describing a menu entry with an action."""

    # This should be calculated from the space necessary for the icon,
    # but we're going to be lazy for now and just assume they're all this big.
    height = 20

    hilight_border_top = (54/255,128/255,205/255)
    hilight_gradient_top = (93/255,163/255,236/255)
    hilight_gradient_bottom = (56/255,137/255,220/55)
    hilight_border_bottom = (47/255,106/255,167/255)

    def __init__(self, title, icon, action=None, data=None):
        self.title = title
        self.icon = get_icon(icon) if icon else None
        self.action = action
        self.data = data
        self.font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,13,0xFF000000)
        self.font_hilight = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF,13,0xFFFFFFFF)
        self.width = self.font.width(self.title) + 50 # Arbitrary bit of extra space.
        # Fit width to hold title?
        self.tr = text_region.TextRegion(0,0,self.width - 22, 20, self.font)
        self.tr.set_text(title)
        self.hilight = False
        self.window = None
        self.gradient = cairo.LinearGradient(0,0,0,self.height-2)
        self.gradient.add_color_stop_rgba(0.0,*self.hilight_gradient_top,1.0)
        self.gradient.add_color_stop_rgba(1.0,*self.hilight_gradient_bottom,1.0)

    def focus_enter(self,keyboard=False):
        if self.window and self.window.child:
            self.window.child.definitely_close()
        self.tr.set_font(self.font_hilight)
        self.tr.set_text(self.title)
        self.hilight = True

    def focus_leave(self):
        self.tr.set_font(self.font)
        self.tr.set_text(self.title)
        self.hilight = False

    def draw(self, window, offset, ctx):
        # Here, offset = y offset, not x like in panel widgets
        # eventually, this all needs to be made generic with containers and calculated window coordinates...
        # but for now, as always, we're being lazy
        self.window = window
        self.offset = offset
        if self.hilight:
            ctx.rectangle(1,offset,window.width-2,1)
            ctx.set_source_rgb(*self.hilight_border_top)
            ctx.fill()
            ctx.rectangle(1,offset+self.height-1,window.width-2,1)
            ctx.set_source_rgb(*self.hilight_border_bottom)
            ctx.fill()
            ctx.save()
            ctx.translate(0,offset+1)
            ctx.rectangle(1,0,window.width-2,self.height-2)
            ctx.set_source(self.gradient)
            ctx.fill()
            ctx.restore()
        if self.icon:
            ctx.save()
            ctx.translate(4,offset+2)
            if self.icon.get_width != 16:
                ctx.scale(16/self.icon.get_width(),16/self.icon.get_width())
            ctx.set_source_surface(self.icon,0,0)
            ctx.paint()
            ctx.restore()
        self.tr.move(22,offset+2)
        self.tr.draw(window)

    def activate(self):
        if self.action:
            self.action(self.data) # Probably like launch_app("terminal")
            self.focus_leave()
            self.window.root.hovered_menu = None
            m = [m for m in self.window.root.menus.values()]
            for k in m:
                k.definitely_close()

    def mouse_action(self, msg):
        if msg.command == yutani.MouseEvent.CLICK:
            self.activate()

        return False

class MenuEntrySubmenu(MenuEntryAction):
    """A menu entry which opens a nested submenu."""

    def __init__(self, title, entries):
        super(MenuEntrySubmenu,self).__init__(title,"folder",None,None)
        self.entries = entries

    def focus_enter(self,keyboard=False):
        self.tr.set_font(self.font_hilight)
        self.tr.set_text(self.title)
        self.hilight = True
        if not keyboard:
            self.activate()

    def activate(self):
        if self.window:
            menu = MenuWindow(self.entries, (self.window.x + self.window.width - 2, self.window.y + self.offset - self.window.top_height), self.window, root=self.window.root)
    def mouse_action(self, msg):
        return False

class MenuEntryDivider(object):
    """A visible separator between menu entries. Does nothing."""

    height = 6
    width = 0

    def draw(self, window, offset, ctx):
        self.window = window
        ctx.rectangle(2,offset+3,window.width-4,1)
        ctx.set_source_rgb(0.7,0.7,0.7)
        ctx.fill()
        ctx.rectangle(2,offset+4,window.width-5,1)
        ctx.set_source_rgb(0.98,0.98,0.98)
        ctx.fill()

    def focus_enter(self,keyboard=False):
        if self.window and self.window.child:
            self.window.child.definitely_close()
        pass

    def focus_leave(self):
        pass

    def mouse_action(self,msg):
        return False

class MenuWindow(yutani.Window):
    """Nested menu window."""

    # These should be part of some theming functionality, but for now we'll
    # just embed them here in the MenuWindow class.
    top_height = 4
    bottom_height = 4
    base_background = (239/255,238/255,232/255)
    base_border = (109/255,111/255,112/255)

    def __init__(self, entries, origin=(0,0), parent=None, root=None):
        self.parent = parent
        if self.parent:
            self.parent.child = self
        self.entries = entries
        required_width = max([e.width for e in entries])
        required_height = sum([e.height for e in entries]) + self.top_height + self.bottom_height
        flags = yutani.WindowFlag.FLAG_ALT_ANIMATION
        super(MenuWindow, self).__init__(required_width,required_height,doublebuffer=True,flags=flags)
        self.move(*origin)
        self.focused_widget = None
        self.child = None
        self.x, self.y = origin
        self.closed = False
        self.root = root
        self.root.menus[self.wid] = self
        self.draw()

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)

        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.rectangle(0,0,self.width,self.height)
        ctx.set_line_width(2)
        ctx.set_source_rgb(*self.base_background)
        ctx.fill_preserve()
        ctx.set_source_rgb(*self.base_border)
        ctx.stroke()
        ctx.set_operator(cairo.OPERATOR_OVER)

        offset = self.top_height
        for entry in self.entries:
            entry.draw(self,offset,ctx)
            offset += entry.height


        self.flip()

    def mouse_action(self, msg):
        if msg.new_y < self.top_height or msg.new_y >= self.height - self.bottom_height or \
            msg.new_x < 0 or msg.new_x >= self.width:
            if self.focused_widget:
                self.focused_widget.focus_leave()
                self.focused_widget = None
                self.draw()
            return
        # We must have focused something
        x = (msg.new_y - self.top_height)
        offset = 0
        new_widget = None
        for entry in self.entries:
            if x >= offset and x < offset + entry.height:
                new_widget = entry
                break
            offset += entry.height

        redraw = False
        if new_widget:
            if self.focused_widget != new_widget:
                if self.focused_widget:
                    self.focused_widget.focus_leave()
                new_widget.focus_enter(keyboard=False)
                self.focused_widget = new_widget
                redraw = True
            if new_widget.mouse_action(msg):
                redraw = True
        if redraw:
            self.draw()

    def has_eventual_child(self, child):
        """Does this menu have the given menu as a child, or a child of a child, etc.?"""
        if child is self: return True
        if not self.child: return False
        return self.child.has_eventual_child(child)

    def keyboard_event(self, msg):
        """Handle keyboard."""
        if msg.event.action != yutani.KeyAction.ACTION_DOWN:
            return
        if msg.event.keycode == yutani.Keycode.ESCAPE:
            self.root.hovered_menu = None
            self.root.menus[msg.wid].leave_menu()
            return

        self.root.hovered_menu = self

        if msg.event.keycode == yutani.Keycode.ARROW_DOWN:
            if not self.focused_widget and self.entries:
                self.focused_widget = self.entries[0]
                self.focused_widget.focus_enter(keyboard=True)
                self.draw()
                return
            i = 0
            for entry in self.entries:
                if entry == self.focused_widget:
                    break
                i += 1
            i += 1
            if i >= len(self.entries):
                i = 0
            self.focused_widget.focus_leave()
            self.focused_widget = self.entries[i]
            self.focused_widget.focus_enter(keyboard=True)
            self.draw()

        if msg.event.keycode == yutani.Keycode.ARROW_UP:
            if not self.focused_widget and self.entries:
                self.focused_widget = self.entries[0]
                self.focused_widget.focus_enter(keyboard=True)
                self.draw()
                return
            i = 0
            for entry in self.entries:
                if entry == self.focused_widget:
                    break
                i += 1
            i -= 1
            if i < 0:
                i = len(self.entries)-1
            self.focused_widget.focus_leave()
            self.focused_widget = self.entries[i]
            self.focused_widget.focus_enter(keyboard=True)
            self.draw()

        if msg.event.keycode == yutani.Keycode.ARROW_LEFT:
            self.root.hovered_menu = self.parent
            self.definitely_close()
            return

        if msg.event.keycode == yutani.Keycode.ARROW_RIGHT:
            if self.focused_widget and isinstance(self.focused_widget, MenuEntrySubmenu):
                self.focused_widget.activate()

        if msg.event.key == b'\n':
            if self.focused_widget:
                self.focused_widget.activate()

    def definitely_close(self):
        """Close this menu and all of its submenus."""
        if self.child:
            self.child.definitely_close()
            self.child = None
        if self.closed:
            return
        if self.focused_widget:
            self.focused_widget.focus_leave()
        self.closed = True
        wid = self.wid
        self.close()
        del self.root.menus[wid]

    def leave_menu(self):
        """Focus has left this menu. If it is not a parent of the currently active menu, close it."""
        if self.has_eventual_child(self.root.hovered_menu):
            pass
        else:
            m = [m for m in self.root.menus.values()]
            for k in m:
                if not self.root.hovered_menu or (k is not self.root.hovered_menu.child and not k.has_eventual_child(self.root.hovered_menu)):
                    k.definitely_close()

