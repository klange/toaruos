#!/usr/bin/python3
"""
Help Documentation Browser
"""
import os
import sys
import subprocess

import cairo

import yutani
import toaru_fonts
import text_region

from menu_bar import MenuBarWidget, MenuEntryAction, MenuEntrySubmenu, MenuEntryDivider, MenuWindow
from about_applet import AboutAppletWindow
from dialog import DialogWindow

import yutani_mainloop

app_name = "Help Browser"
version = "1.0.0"
_description = f"<b>{app_name} {version}</b>\n© 2017 Kevin Lange\n\nRich text help document viewer.\n\n<color 0x0000FF>http://github.com/klange/toaruos</color>"


class ScrollableText(object):

    def __init__(self):
        self.tr = None
        self.width = 0
        self.height_ext = 0
        self.height_int = 0
        self.text_buffer = None
        self.background = (1,1,1)
        self.pad = 10

    def destroy(self):
        if self.text_buffer:
            self.text_buffer.destroy()

    def update(self, width):

        needs_resize = False

        if width != self.width:
            needs_resize = True
            self.width = width

        self.tr.resize(self.width-self.pad*2, self.tr.line_height)
        h = self.tr.line_height * len(self.tr.lines) + self.pad*2
        if h != self.height_int:
            needs_resize = True
            self.height_int = h

        if self.height_int - self.pad * 2 > 30000:
            # Shit...
            self.height_int = 30000 - self.pad * 2

        self.tr.resize(self.width-self.pad*2, self.height_int-self.pad*2)
        self.tr.move(self.pad,self.pad)

        if needs_resize or not self.text_buffer:
            if self.text_buffer:
                self.text_buffer.destroy()
            self.text_buffer = yutani.GraphicsBuffer(self.width,self.height_int)

        surface = self.text_buffer.get_cairo_surface()
        ctx = cairo.Context(surface)
        ctx.rectangle(0,0,surface.get_width(),surface.get_height())
        ctx.set_source_rgb(*self.background)
        ctx.fill()

        self.tr.draw(self.text_buffer)

    def scroll_max(self):
        if self.height_ext > self.height_int:
            return 0

        return self.height_int - self.height_ext


    def draw(self,ctx,x,y,height,scroll):
        self.height_ext = height
        surface = self.text_buffer.get_cairo_surface()
        ctx.rectangle(x,y,self.width,height)
        ctx.set_source_surface(surface,x,y-scroll)
        ctx.fill()

class HelpBrowserWindow(yutani.Window):

    base_width = 800
    base_height = 600

    def __init__(self, decorator):
        super(HelpBrowserWindow, self).__init__(self.base_width + decorator.width(), self.base_height + decorator.height(), title=app_name, icon="help", doublebuffer=True)
        self.move(100,100)
        self.x = 100
        self.y = 100
        self.decorator = decorator
        self.current_topic = "0_index.trt"
        self.text_buffer = None
        self.text_offset = 0
        self.tr = None
        self.size_changed = True
        self.text_scroller = ScrollableText()

        self.special = {}
        self.special['contents'] = self.special_contents
        self.special['demo'] = self.special_demo
        self.down_text = None
        self.cache = {}
        self.history = []
        self.history_index = 0
        self.title_cache = {}

        def herp(action):
            print(action)

        self.history_menu = MenuEntrySubmenu('History...',[MenuEntryDivider()])

        def exit_app(action):
            menus = [x for x in self.menus.values()]
            for x in menus:
                x.definitely_close()
            self.close()
            sys.exit(0)

        def about_window(action):
            AboutAppletWindow(self.decorator,f"About {app_name}","/usr/share/icons/48/help.png",_description,"help")

        menus = [
            ("File", [
                #MenuEntryAction("Open...",None,print_derp,None),
                #MenuEntryDivider(),
                MenuEntryAction("Exit","exit",exit_app,None),
            ]),
            ("Go", [
                MenuEntryAction("Home","home",self.go_page,"0_index.trt"),
                MenuEntryAction("Topics","bookmark",self.go_page,"special:contents"),
                MenuEntryDivider(),
                self.history_menu,
                MenuEntryAction("Back","back",self.go_back,None),
                MenuEntryAction("Forward","forward",self.go_forward,None),
            ]),
            ("Help", [
                MenuEntryAction("Contents","help",self.go_page,"help_browser.trt"),
                MenuEntryDivider(),
                MenuEntryAction(f"About {app_name}","star",about_window,None),
            ]),
        ]

        self.menubar = MenuBarWidget(self,menus)

        self.menus = {}
        self.hovered_menu = None

        self.update_text_buffer()
        self.navigate("0_index.trt")


    def get_title(self, document):
        if document.startswith("special:"):
            if document[8:] in self.special:
                return self.special[document[8:]].__doc__
            return "???"
        elif document.startswith("http:") or document.startswith('https:'):
            if document in self.title_cache:
                return self.title_cache[document]
            if document in self.cache:
                lines = self.cache[document].split('\n')
                for x in lines:
                    x = x.strip()
                    if x.startswith('<h1>') and x.endswith('</h1>'):
                        return x[4:-5]
                return document.split('/')[-1].replace('.trt','').title()
            else:
                return document
        elif document.startswith("file:"):
            path = document.replace("file:","")
        else:
            path = f'/usr/share/help/{document}'
        if not os.path.exists(path):
            return "(file not found)"
        with open(path,'r') as f:
            lines = f.readlines()
            for x in lines:
                x = x.strip()
                if x.startswith('<h1>') and x.endswith('</h1>'):
                    return x[4:-5]
            return document.replace('.trt','').title()

    def special_contents(self):
        """Table of Contents"""
        # List all things.
        output = "\n<h1>Table of Contents</h1>\n\nThis table of contents is automatically generated.\n\n"
        output += "<h2>Special Pages</h2>\n\n"
        for k in self.special:
            output += f"➤ <link target=\"special:{k}\">{self.special[k].__doc__}</link>\n"
        output += "\n<h2>Documentation</h2>\n\n"
        for k in sorted(os.listdir('/usr/share/help')):
            if k.endswith('.trt'):
                output += f"➤ <link target=\"{k}\">{self.get_title(k)}</link>\n"
        for directory,_,files in os.walk('/usr/share/help'):
            if directory == '/usr/share/help':
                continue
            files = sorted([x for x in files if not x.startswith('.')])
            if files:
                d = directory.replace('/usr/share/help/','')
                output += "\n<h3>" + d.title() + "</h3>\n\n"
                for k in files:
                    if k.endswith('.trt'):
                        k = d + '/' + k
                        output += f"➤ <link target=\"{k}\">{self.get_title(k)}</link>\n"
        return output

    def special_demo(self):
        """Formatting demo"""
        return f"""

<h1>This is a big header</h1>
This is text below that.
<h2>This is a medium header</h2>

<h3>This is a small header</h3>

This is normal text. <b>This is bold text.</b> <i>This is italic text.</i> <b><i>This is both.</i></b>
<link target=\"0_index.trt\">go home</link>"""

    def get_cache(self, url):
        if url in self.cache:
            return self.cache[url]
        else:
            try:
                text = subprocess.check_output(['fetch',url])
                if text.startswith(b'\x89PNG'):
                    text = f"<html><body><img src=\"{url}\"></body></html>"
                else:
                    text = text.decode('utf-8')
            except:
                text = '\n<h1>Error</h1>\n\nThere was an error obtaining this file.'
            self.cache[url] = text
            return text

    def get_document_text(self):
        if self.current_topic.startswith("special:"):
            if self.current_topic[8:] in self.special:
                return self.special[self.current_topic[8:]]()
        elif self.current_topic.startswith("http:") or self.current_topic.startswith('https:'):
            # Good luck
            return self.get_cache(self.current_topic)
        elif self.current_topic.startswith("file:"):
            path = self.current_topic.replace("file:","")
        else:
            path = f'/usr/share/help/{self.current_topic}'
        if os.path.exists(path):
            with open(path,'r') as f:
                return f.read()
        return f"""
<h1>Document Not Found</h1>

Uh oh, looks like the help document you tried to open ({self.current_topic}) wasn't available. Do you want to <link target=\"0_index.trt\">return to the index</link>?

You can also <link target=\"special:contents\">check the Table of Contents</link>.

"""

    def is_html(self):
        if self.current_topic.endswith('.html') or self.current_topic.endswith('.htm'): return True
        if self.current_topic.startswith('http') and not self.current_topic.endswith('.trt'): return True
        if '<html' in self.get_document_text(): return True
        return False

    def update_history(self):
        def go_history(action):
            self.navigate(self.history[action],touch_history=False)
            self.history_index = action
            self.update_history()
        entries = []
        for x in range(len(self.history)):
            t = self.get_title(self.history[x])
            e = MenuEntryAction(t,None,go_history,x)
            if x == self.history_index:
                e.title = f'<b>{t}</b>'
                e.rich = True
                e.update_text()
            entries.append(e)
        entries.reverse()
        self.history_menu.entries = entries

    def navigate(self, target, touch_history=True):
        #if target.startswith('https:'):
        #    DialogWindow(self.decorator,app_name,f"<mono>https</mono> is not supported. Could not load the URL <mono>{target}</mono>",callback=lambda: None,window=self,cancel_label=False)
        #    return
        if touch_history:
            del self.history[self.history_index+1:]
            self.history.append(target)
            self.history_index = len(self.history)-1
        self.current_topic = target
        self.text_offset = 0
        if self.is_html():
            self.tr.base_dir = os.path.dirname(target) + '/'
        else:
            self.tr.base_dir = '/usr/share/help/'
        self.tr.set_richtext(self.get_document_text(),html=self.is_html())
        self.update_text_buffer()
        if self.tr.title:
            self.set_title(f"{self.tr.title} - {app_name}","help")
            self.title_cache[target] = self.tr.title
        else:
            self.set_title(f"{self.get_title(self.current_topic)} - {app_name}","help")
        self.update_history()

    def update_text_buffer(self):
        if not self.tr:
            self.tr = text_region.TextRegion(0,0,100,100)
            self.tr.set_line_height(18)
            self.tr.base_dir = '/usr/share/help/'
            self.tr.set_richtext(self.get_document_text(),html=self.is_html())
            self.text_scroller.tr = self.tr

        if self.size_changed:
            self.text_scroller.update(self.width - self.decorator.width())

        #self.tr.scroll = self.scroll_offset
        #self.tr.draw(self.text_buffer)

    def draw(self):
        surface = self.get_cairo_surface()

        WIDTH, HEIGHT = self.width - self.decorator.width(), self.height - self.decorator.height()

        ctx = cairo.Context(surface)
        ctx.translate(self.decorator.left_width(), self.decorator.top_height())
        ctx.rectangle(0,0,WIDTH,HEIGHT)
        #ctx.set_source_rgb(204/255,204/255,204/255)
        ctx.set_source_rgb(1,1,1)
        ctx.fill()

        ctx.save()
        ctx.translate(0,self.menubar.height)
        """
        text = self.text_buffer.get_cairo_surface()
        ctx.set_source_surface(text,0,-self.text_offset)
        ctx.paint()
        """
        self.text_scroller.draw(ctx,0,0,HEIGHT-self.menubar.height,self.text_offset)
        ctx.restore()

        self.menubar.draw(ctx,0,0,WIDTH)

        self.decorator.render(self)
        self.flip()

    def finish_resize(self, msg):
        """Accept a resize."""
        if msg.width < 100 or msg.height < 100:
            self.resize_offer(max(msg.width,100),max(msg.height,100))
            return

        self.resize_accept(msg.width, msg.height)
        self.reinit()
        self.size_changed = True
        self.update_text_buffer()
        self.draw()
        self.resize_done()
        self.flip()

    def scroll(self, amount):
        self.text_offset += amount
        if self.text_offset < 0:
            self.text_offset = 0
        if self.text_offset > self.text_scroller.scroll_max():
            self.text_offset = self.text_scroller.scroll_max()

    def text_under_cursor(self, msg):
        """Get the text unit under the cursor."""
        x = msg.new_x - self.decorator.left_width()
        y = msg.new_y - self.decorator.top_height() + self.text_offset - self.menubar.height
        return self.tr.click(x,y)


    def go_page(self, action):
        """Navigate to a page."""
        self.navigate(action)
        self.draw()

    def go_back(self,action):
        """Go back."""
        if self.history and self.history_index > 0:
            self.history_index -= 1
            self.navigate(self.history[self.history_index], touch_history=False)
            self.update_history()
        self.draw()

    def go_forward(self,action):
        """Go forward."""
        if self.history and self.history_index < len(self.history)-1:
            self.history_index += 1
            self.navigate(self.history[self.history_index], touch_history=False)
            self.update_history()
        self.draw()

    def mouse_event(self, msg):
        if self.mouse_check(msg):
            self.draw()

    def mouse_check(self, msg):
        if d.handle_event(msg) == yutani.Decor.EVENT_CLOSE:
            window.close()
            sys.exit(0)
        x,y = msg.new_x - self.decorator.left_width(), msg.new_y - self.decorator.top_height()
        w,h = self.width - self.decorator.width(), self.height - self.decorator.height()
        if x >= 0 and x < w and y >= 0 and y < self.menubar.height:
            self.menubar.mouse_event(msg, x, y)

        if x >= 0 and x < w and y >= self.menubar.height and y < h:
            if msg.buttons & yutani.MouseButton.BUTTON_RIGHT:
                if not self.menus:
                    menu_entries = [
                        MenuEntryAction("Back","back",self.go_back,None),
                        MenuEntryAction("Forward","forward",self.go_forward,None),
                    ]
                    menu = MenuWindow(menu_entries,(self.x+msg.new_x,self.y+msg.new_y),root=self)
            if msg.buttons & yutani.MouseButton.SCROLL_UP:
                self.scroll(-30)
                return True
            elif msg.buttons & yutani.MouseButton.SCROLL_DOWN:
                self.scroll(30)
                return True
            if msg.command == yutani.MouseEvent.DOWN:
                e = self.text_under_cursor(msg)
                r = False
                if self.down_text and e != self.down_text:
                    for u in self.down_text.tag_group:
                        if u.unit_type == 4:
                            u.set_extra('hilight',False)
                        else:
                            u.set_font(self.down_font[u])
                    del self.down_font
                    self.down_text = None
                    self.update_text_buffer()
                    r = True
                if e and 'link' in e.extra and e.tag_group:
                    self.down_font = {}
                    for u in e.tag_group:
                        if u.unit_type == 4:
                            u.set_extra('hilight',True)
                        else:
                            new_font = toaru_fonts.Font(u.font.font_number,u.font.font_size,0xFFFF0000)
                            self.down_font[u] = u.font
                            u.set_font(new_font)
                    self.update_text_buffer()
                    r = True
                    self.down_text = e
                else:
                    self.down_text = None
                return r
            if msg.command == yutani.MouseEvent.CLICK or msg.command == yutani.MouseEvent.RAISE:
                e = self.text_under_cursor(msg)
                if self.down_text and e == self.down_text:
                    self.navigate(e.extra['link'])
                    return True
                elif self.down_text:
                    for u in self.down_text.tag_group:
                        if u.unit_type == 4:
                            u.set_extra('hilight',False)
                        else:
                            u.set_font(self.down_font[u])
                    del self.down_font
                    self.down_text = None
                    self.update_text_buffer()
                    return True

        return False

    def keyboard_event(self, msg):
        if self.keyboard_check(msg):
            self.draw()

    def keyboard_check(self,msg):
        if msg.event.action != 0x01:
            return False # Ignore anything that isn't a key down.
        if msg.event.keycode == yutani.Keycode.HOME:
            self.text_offset = 0
            return True
        elif msg.event.keycode == yutani.Keycode.END:
            n = (len(self.tr.lines)-self.tr.visible_lines())+5
            self.text_offset = self.text_scroller.scroll_max()
            return True
        elif msg.event.keycode == yutani.Keycode.PAGE_UP:
            self.scroll(int(-self.height/2))
            return True
        elif msg.event.keycode == yutani.Keycode.PAGE_DOWN:
            self.scroll(int(self.height/2))
            return True
        elif msg.event.key == b"q":
            self.close()
            sys.exit(0)

if __name__ == '__main__':
    yutani.Yutani()
    d = yutani.Decor()

    window = HelpBrowserWindow(d)

    if len(sys.argv) > 1:
        window.navigate(sys.argv[-1])

    window.draw()

    yutani_mainloop.mainloop()
