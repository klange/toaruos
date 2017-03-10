import hashlib
import subprocess
from urllib.parse import urlparse
import unicodedata
from html.parser import HTMLParser
import math
import os

import cairo
import toaru_fonts

_emoji_available = os.path.exists('/usr/share/emoji')

if _emoji_available:
    _emoji_values = [int(x.replace('.png',''),16) for x in os.listdir('/usr/share/emoji') if x.endswith('.png') and not '-' in x]

_emoji_table = {}
def get_emoji(emoji):
    if not emoji in _emoji_table:
        _emoji_table[emoji] = cairo.ImageSurface.create_from_png('/usr/share/emoji/' + hex(ord(emoji)).replace('0x','')+'.png')
    return _emoji_table[emoji]

class TextUnit(object):
    def __init__(self, string, unit_type, font):
        self.string = string
        self.unit_type = unit_type
        self.font = font
        self.width = font.width(self.string) if font else 0
        self.extra = {}
        self.tag_group = None
        if self.unit_type == 2 and _emoji_available:
            if ord(self.string) > 0x1000 and ord(self.string) in _emoji_values:
                self.extra['emoji'] = True
                self.extra['img'] = get_emoji(self.string)
                self.extra['offset'] = font.font_size
                self.string = ""
                self.width = font.font_size

    def set_tag_group(self, tag_group):
        self.tag_group = tag_group
        self.tag_group.append(self)

    def set_font(self, font):
        if 'img' in self.extra: return
        self.font = font
        self.width = font.width(self.string) if font else 0

    def set_extra(self, key, data):
        self.extra[key] = data

    def __repr__(self):
        return "(" + self.string + "," + str(self.unit_type) + "," + str(self.width) + ")"


class TextRegion(object):

    def __init__(self, x, y, width, height, font=None):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        if not font:
            font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13)
        self.font = font
        self.text = ""
        self.lines = []
        self.align = 0
        self.valign = 0
        self.line_height = self.font.font_size
        self.text_units = []
        self.scroll = 0
        self.ellipsis = ""
        self.one_line = False
        self.base_dir = ""
        self.break_all = False
        self.title = None

    def set_alignment(self, align):
        self.align = align

    def set_valignment(self, align):
        self.valign = align

    def visible_lines(self):
        return int(self.height / self.line_height)

    def reflow(self):
        self.lines = []

        current_width = 0
        current_units = []
        leftover = None

        i = 0
        while i < len(self.text_units):
            if leftover:
                unit = leftover
                leftover = None
            else:
                unit = self.text_units[i]
            if unit.unit_type == 3:
                self.lines.append(current_units)
                current_units = []
                current_width = 0
                i += 1
                continue
            if unit.unit_type == 4:
                if current_units:
                    self.lines.append(current_units)
                    i += 1
                self.lines.append([unit])
                current_units = []
                current_width = 0
                i += 1
                continue

            if current_width + unit.width > self.width:
                if not current_units or self.one_line:
                    # We need to split the current unit.
                    k = len(unit.string)-1
                    while k and current_width + unit.font.width(unit.string[:k] + self.ellipsis) > self.width:
                        k -= 1
                    ellipsis = self.ellipsis
                    if not k and self.ellipsis:
                        ellipsis = ""
                    if not k and self.one_line:
                        added_ellipsis = False
                        while len(current_units) and sum([unit.width for unit in current_units]) + unit.font.width(self.ellipsis) > self.width:
                            this_unit = current_units[-1]
                            current_units = current_units[:-1]
                            current_width = sum([unit.width for unit in current_units])
                            k = len(this_unit.string)-1
                            while k and current_width + unit.font.width(this_unit.string[:k] + self.ellipsis) > self.width:
                                k -= 1
                            if k:
                                current_units.append(TextUnit(this_unit.string[:k] + self.ellipsis,this_unit.unit_type,this_unit.font))
                                added_ellipsis = True
                                break
                        if not added_ellipsis:
                            current_units.append(TextUnit(self.ellipsis,0,unit.font))

                    else:
                        current_units.append(TextUnit(unit.string[:k]+ellipsis,unit.unit_type,unit.font))
                    leftover = TextUnit(unit.string[k:],unit.unit_type,unit.font)
                    self.lines.append(current_units)
                    current_units = []
                    current_width = 0
                    if self.one_line:
                        return
                else:
                    self.lines.append(current_units)
                    current_units = []
                    current_width = 0
                    if unit.unit_type == 1:
                        i += 1
            else:
                current_units.append(unit)
                current_width += unit.width
                i += 1
        if current_units:
            self.lines.append(current_units)

    def units_from_text(self, text, font=None, whitespace=True):
        if not font:
            font = self.font

        def char_width(char):
            if _emoji_available and ord(char) in _emoji_values:
                return 2
            x = unicodedata.east_asian_width(char)
            if x == 'Na': return 1 # Narrow
            if x == 'N': return 1 # Narrow
            if x == 'A': return 1 # Ambiguous
            if x == 'W': return 2 # Wide
            if x == 'F': return 1 # Fullwidth (treat as normal)
            if x == 'H': return 1 # Halfwidth
            print(f"Don't know how wide {x} is, assuming 1")
            return 1

        def classify(char):
            if char == '\n': return 3 # break on line feed
            if unicodedata.category(char) == 'Zs': return 1 # break on space
            if char_width(char) > 1: return 2 # allow break on CJK characters (TODO: only really valid for Chinese and Japanese; Korean doesn't work this way
            if self.break_all: return 2
            return 0

        units = []
        offset = 0
        current_unit = ""
        while offset < len(text):
            c = text[offset]
            if not whitespace and c.isspace():
                if current_unit:
                    units.append(TextUnit(current_unit,0,font))
                    current_unit = ""
                units.append(TextUnit(' ',1,font))
                offset += 1
                continue
            x = classify(c)
            if x == 0:
                current_unit += c
                offset += 1
            else:
                if not current_unit:
                    units.append(TextUnit(c,x,font))
                    offset += 1
                else:
                    units.append(TextUnit(current_unit,0,font))
                    current_unit = ""
        if current_unit:
            units.append(TextUnit(current_unit,0,font))
        return units

    def set_one_line(self, one_line=True):
        self.one_line = one_line
        self.reflow()

    def set_ellipsis(self, ellipsis="…"):
        self.ellipsis = ellipsis
        self.reflow()

    def set_text(self, text):
        self.text = text
        self.text_units = self.units_from_text(text)
        self.reflow()

    def set_richtext(self, text, html=False):
        f = self.font
        self.text = text
        tr = self

        class RichTextParser(HTMLParser):

            def __init__(self, html=False):
                super(RichTextParser,self).__init__()
                self.font_stack = []
                self.tag_stack = []
                self.current_font = f
                self.units = []
                self.link_stack = []
                self.current_link = None
                self.tag_group = None
                self.is_html = html
                self.whitespace_sensitive = not html
                self.autoclose = ['br','meta','input']
                self.title = ''
                if self.is_html:
                    self.autoclose.extend(['img','link'])
                self.surface_cache = {}

            def handle_starttag(self, tag, attrs):
                def make_bold(n):
                    if n == 0: return 1
                    if n == 2: return 3
                    if n == 4: return 5
                    if n == 6: return 7
                    return n
                def make_italic(n):
                    if n == 0: return 2
                    if n == 1: return 3
                    if n == 4: return 6
                    if n == 5: return 7
                    return n
                def make_monospace(n):
                    if n == 0: return 4
                    if n == 1: return 5
                    if n == 2: return 6
                    if n == 3: return 7
                    return n

                if tag not in self.autoclose:
                    self.tag_stack.append(tag)

                if tag in ['p','div','h1','h2','h3','li','tr','pre'] and not self.whitespace_sensitive: # etc?
                    if self.units and self.units[-1].unit_type != 3:
                        self.units.append(TextUnit('\n',3,self.current_font))

                if tag == "b":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_bold(self.current_font.font_number),self.current_font.font_size,self.current_font.font_color)
                elif tag == "i":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_italic(self.current_font.font_number),self.current_font.font_size,self.current_font.font_color)
                elif tag == "color":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(self.current_font.font_number,self.current_font.font_size,int(attrs[0][0],16) | 0xFF000000)
                elif tag == "mono":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_monospace(self.current_font.font_number),self.current_font.font_size,self.current_font.font_color)
                elif tag == "pre":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_monospace(self.current_font.font_number),self.current_font.font_size,self.current_font.font_color)
                elif tag == "link" and not self.is_html:
                    target = None
                    for attr in attrs:
                        if attr[0] == "target":
                            target = attr[1]
                    self.tag_group = []
                    self.link_stack.append(self.current_link)
                    self.current_link = target
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(self.current_font.font_number,self.current_font.font_size,0xFF0000FF)
                elif tag == "a":
                    target = None
                    for attr in attrs:
                        if attr[0] == "href":
                            target = attr[1]
                    self.tag_group = []
                    self.link_stack.append(self.current_link)
                    if target and self.is_html and not target.startswith('http:') and not target.startswith('https:'):
                        # This is actually more complicated than this check - protocol-relative stuff can work without full URLs
                        if target.startswith('/'):
                            base = urlparse(tr.base_dir)
                            target = f"{base.scheme}://{base.netloc}{target}"
                        else:
                            target = tr.base_dir + target
                    self.current_link = target
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(self.current_font.font_number,self.current_font.font_size,0xFF0000FF)
                elif tag == "h1":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_bold(self.current_font.font_number),20)
                elif tag == "h2":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_bold(self.current_font.font_number),18)
                elif tag == "h3":
                    self.font_stack.append(self.current_font)
                    self.current_font = toaru_fonts.Font(make_bold(self.current_font.font_number),16)
                elif tag == "img":
                    self.handle_img(tag,attrs)
                elif tag == "br":
                    units = tr.units_from_text('\n', self.current_font)
                    self.units.extend(units)
                else:
                    pass

            def handle_startendtag(self, tag, attrs):
                if tag == "img":
                    self.handle_img(tag,attrs)
                elif tag == "br":
                    units = tr.units_from_text('\n', self.current_font)
                    self.units.extend(units)
                elif tag in ['p','div','h1','h2','h3','tr','pre'] and not self.whitespace_sensitive: # etc?
                    units = tr.units_from_text('\n', self.current_font)
                    self.units.extend(units)
                else:
                    # Unknown start/end tag.
                    pass


            def handle_endtag(self, tag):
                if not self.tag_stack:
                    print(f"No stack when trying to close {tag}?")
                if self.tag_stack[-1] != tag:
                    print(f"unclosed tag {self.tag_stack[-1]} when closing tag {tag}")
                else:
                    self.tag_stack.pop()
                    if tag in ["b","i","color","mono","link","h1","h2","h3","a","pre"]:
                        self.current_font = self.font_stack.pop()
                    if tag in ['p','div','h1','h2','h3','li','tr','pre'] and not self.whitespace_sensitive: # etc?
                        units = tr.units_from_text('\n', self.current_font)
                        self.units.extend(units)
                    if tag in ["link","a"]:
                        self.current_link = self.link_stack.pop()
                        self.tag_group = None

            def handle_data(self, data):
                if 'title' in self.tag_stack:
                    self.title += data
                if 'head' in self.tag_stack or 'script' in self.tag_stack: return
                if 'pre' in self.tag_stack:
                    units = tr.units_from_text(data, self.current_font, whitespace=True)
                else:
                    units = tr.units_from_text(data, self.current_font, whitespace=self.whitespace_sensitive)
                if self.current_link:
                    for u in units:
                        u.set_extra('link',self.current_link)
                if self.tag_group is not None:
                    for u in units:
                        u.set_tag_group(self.tag_group)
                self.units.extend(units)

            def handle_img(self, tag, attrs):
                target = None
                for attr in attrs:
                    if attr[0] == "src":
                        target = attr[1]
                if target and self.is_html and not target.startswith('http:') and not target.startswith('https:'):
                    # This is actually more complicated than this check - protocol-relative stuff can work without full URLs
                    if target.startswith('/'):
                        base = urlparse(tr.base_dir)
                        target = f"{base.scheme}://{base.netloc}{target}"
                    else:
                        target = tr.base_dir + target
                else:
                    if target and not self.is_html and not target.startswith('/'):
                        target = tr.base_dir + target
                    if target and self.is_html and not target.startswith('http:'):
                        target = tr.base_dir + target
                if target and target.startswith('http:'):
                    x = hashlib.sha512(target.encode('utf-8')).hexdigest()
                    p = f'/tmp/.browser-cache.{x}'
                    if not os.path.exists(p):
                        try:
                            subprocess.check_output(['fetch','-o',p,target])
                        except:
                            print(f"Failed to download image: {target}")
                            pass
                    target = p
                if target and os.path.exists(target):
                    try:
                        img = self.img_from_path(target)
                    except:
                        print(f"Failed to load image {target}, going to show backup image.")
                        img = self.img_from_path('/usr/share/icons/16/help.png')
                    chop = math.ceil(img.get_height() / tr.line_height)
                    group = []
                    for i in range(chop):
                        u = TextUnit("",4,self.current_font)
                        u.set_extra('img',img)
                        u.set_extra('offset',i * tr.line_height)
                        if self.current_link:
                            u.set_extra('link',self.current_link)
                        u.set_tag_group(group)
                        u.width = img.get_width()
                        self.units.append(u)

            def fix_whitespace(self):
                out_units = []
                last_was_whitespace = False
                for unit in self.units:
                    if unit.unit_type == 3:
                        last_was_whitespace = True
                        out_units.append(unit)
                    elif unit.unit_type == 1 and unit.string == ' ':
                        if last_was_whitespace:
                            continue
                        last_was_whitespace = True
                        out_units.append(unit)
                    else:
                        last_was_whitespace = False
                        out_units.append(unit)
                self.units = out_units

            def img_from_path(self, path):
                if not path in self.surface_cache:
                    s = cairo.ImageSurface.create_from_png(path)
                    self.surface_cache[path] = s
                    return s
                else:
                    return self.surface_cache[path]

        parser = RichTextParser(html=html)
        parser.feed(text)
        self.title = parser.title
        if html:
            parser.fix_whitespace()
        self.text_units = parser.units
        self.reflow()

    def set_font(self, new_font):
        self.font = new_font
        self.line_height = self.font.font_size
        self.reflow()

    def set_line_height(self, new_line_height):
        self.line_height = new_line_height
        self.reflow()

    def resize(self, new_width, new_height):
        needs_reflow = self.width != new_width
        self.width = new_width
        self.height = new_height
        if needs_reflow:
            self.reflow()

    def move(self, new_x, new_y):
        self.x = new_x
        self.y = new_y

    def get_offset_at_index(self, index):
        """ Only works for one-liners... """
        if not self.lines:
            return None, (0, 0, 0, 0)
        left_align = 0
        xline = self.lines[0]
        if self.align == 1: # right align
            left_align = self.width - sum([u.width for u in xline])
        elif self.align == 2: # center
            left_align = int((self.width - sum([u.width for u in xline])) / 2)
        i = 0
        for unit in xline:
            if i == index:
                return unit, (0, left_align, left_align, i)
            left_align += unit.width
            i += 1
        return None, (0, left_align, left_align, i)

    def pick(self, x, y):
        # Determine which line this click belongs in
        if x < self.x or x > self.x + self.width or y < self.y or y > self.y + self.height:
            return None, None
        top_align = 0
        if len(self.lines) < int(self.height / self.line_height):
            if self.valign == 1: # bottom
                top_align = self.height - len(self.lines) * self.line_height
            elif self.valign == 2: # middle
                top_align = int((self.height - len(self.lines) * self.line_height) / 2)
        new_y = y - top_align - self.y - 2 # fuzz factor
        line = int(new_y / self.line_height)
        if line < len(self.lines[self.scroll:]):
            left_align = 0
            xline = self.lines[self.scroll+line]
            if self.align == 1: # right align
                left_align = self.width - sum([u.width for u in xline])
            elif self.align == 2: # center
                left_align = int((self.width - sum([u.width for u in xline])) / 2)
            i = 0
            for unit in xline:
                if x >= self.x + left_align and x < self.x + left_align + unit.width:
                    return unit, (line, left_align, x - self.x, i)
                left_align += unit.width
                i += 1
            return None, (line, left_align, x - self.x, i)
        return None, None

    def click(self, x, y):
        unit, _ = self.pick(x,y)
        return unit

    def draw(self, context):
        current_height = self.line_height
        top_align = 0
        if len(self.lines) < int(self.height / self.line_height):
            if self.valign == 1: # bottom
                top_align = self.height - len(self.lines) * self.line_height
            elif self.valign == 2: # middle
                top_align = int((self.height - len(self.lines) * self.line_height) / 2)
        su = context.get_cairo_surface() if 'get_cairo_surface' in dir(context) else None
        cr = cairo.Context(su) if su else None
        for line in self.lines[self.scroll:]:
            if current_height > self.height:
                break
            left_align = 0
            if self.align == 1: # right align
                left_align = self.width - sum([u.width for u in line])
            elif self.align == 2: # center
                left_align = int((self.width - sum([u.width for u in line])) / 2)
            for unit in line:
                if unit.unit_type == 4:
                    cr.save()
                    extra = 3
                    cr.translate(self.x + left_align, self.y + current_height + top_align)
                    if 'hilight' in unit.extra and unit.extra['hilight']:
                        cr.rectangle(0,-self.line_height+extra,unit.extra['img'].get_width(),self.line_height)
                        cr.set_source_rgb(1,0,0)
                        cr.fill()
                    cr.rectangle(0,-self.line_height+extra,unit.extra['img'].get_width(),self.line_height)
                    cr.set_source_surface(unit.extra['img'],0,-unit.extra['offset']-self.line_height+extra)
                    cr.fill()
                    cr.restore()
                elif unit.unit_type == 2 and 'emoji' in unit.extra:
                    cr.save()
                    extra = 3
                    cr.translate(self.x + left_align, self.y + current_height + top_align -self.line_height+extra)
                    if unit.extra['img'].get_height() > self.line_height - 3:
                        scale = (self.line_height - 3) / unit.extra['img'].get_height()
                        cr.scale(scale,scale)
                    cr.rectangle(0,0,unit.extra['img'].get_width(),unit.extra['img'].get_height())
                    cr.set_source_surface(unit.extra['img'],0,0)
                    cr.fill()
                    cr.restore()
                elif unit.font:
                    unit.font.write(context, self.x + left_align, self.y + current_height + top_align, unit.string)
                left_align += unit.width
            current_height += self.line_height
