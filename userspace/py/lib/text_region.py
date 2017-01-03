import unicodedata
from html.parser import HTMLParser
import toaru_fonts

class TextUnit(object):
    def __init__(self, string, unit_type, font):
        self.string = string
        self.unit_type = unit_type
        self.font = font
        self.width = font.width(self.string)

    def set_font(self, font):
        self.font = font
        self.width = font.width(self.string)

    def __repr__(self):
        return "(" + self.string + "," + str(self.unit_type) + "," + str(self.width) + ")"


class TextRegion(object):

    def __init__(self, x, y, width, height, font=toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13)):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
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

    def units_from_text(self, text, font=None):
        if not font:
            font = self.font

        def char_width(char):
            x = unicodedata.east_asian_width(char)
            if x == 'Na': return 1
            if x == 'N': return 1
            if x == 'A': return 1
            if x == 'W': return 2
            raise ValueError("Don't know how wide "+x+" is.")

        def classify(char):
            if char == '\n': return 3 # break on line feed
            if unicodedata.category(char) == 'Zs': return 1 # break on space
            if char_width(char) > 1: return 2 # allow break on CJK characters (TODO: only really valid for Chinese and Japanese; Korean doesn't work this way
            return 0

        units = []
        offset = 0
        current_unit = ""
        while offset < len(text):
            c = text[offset]
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

    def set_richtext(self, text):
        f = self.font
        self.text = text
        tr = self

        class RichTextParser(HTMLParser):
            def __init__(self):
                super(RichTextParser,self).__init__()
                self.font_stack = []
                self.tag_stack = []
                self.current_font = f
                self.units = []

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

                self.tag_stack.append(tag)
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
                else:
                    pass

            def handle_endtag(self, tag):
                if self.tag_stack[-1] != tag:
                    print(f"unclosed tag {self.tag_stack[-1]} when closing tag {tag}")
                else:
                    self.tag_stack.pop()
                    if tag in ["b","i","color","mono"]:
                        self.current_font = self.font_stack.pop()

            def handle_data(self, data):
                units = tr.units_from_text(data, self.current_font)
                self.units.extend(units)

        parser = RichTextParser()
        parser.feed(text)
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

    def click(self, x, y):
        # Determine which line this click belongs in
        if x < self.x or x > self.x + self.width or y < self.y or y > self.y + self.height:
            return None
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
            for unit in xline:
                if x >= self.x + left_align and x < self.x + left_align + unit.width:
                    return unit
                left_align += unit.width
        else:
            return None

    def draw(self, context):
        current_height = self.line_height
        top_align = 0
        if len(self.lines) < int(self.height / self.line_height):
            if self.valign == 1: # bottom
                top_align = self.height - len(self.lines) * self.line_height
            elif self.valign == 2: # middle
                top_align = int((self.height - len(self.lines) * self.line_height) / 2)
        for line in self.lines[self.scroll:]:
            if current_height > self.height:
                break
            left_align = 0
            if self.align == 1: # right align
                left_align = self.width - sum([u.width for u in line])
            elif self.align == 2: # center
                left_align = int((self.width - sum([u.width for u in line])) / 2)
            for unit in line:
                unit.font.write(context, self.x + left_align, self.y + current_height + top_align, unit.string)
                left_align += unit.width
            current_height += self.line_height
