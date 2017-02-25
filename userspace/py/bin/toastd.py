#!/usr/bin/python3
"""
Python implementation of the toast daemon.
"""
import ctypes
import math
import os
import sys
import time

import cairo

import pex
import yutani
import text_region
import toaru_fonts
import fswait
import toast

import yutani_mainloop

notifications = []
sliding = []

background = cairo.ImageSurface.create_from_png("/usr/share/ttk/toast/default.png")

title_font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF_BOLD, 13, 0xFFFFFFFF)
content_font = toaru_fonts.Font(toaru_fonts.FONT_SANS_SERIF, 13, 0xFFFFFFFF)

class Notification(yutani.Window):

    base_width = 310
    base_height = 110
    slide_time = 0.5

    def __init__(self, raw):
        self.ttl = time.time() + raw.ttl
        strings = ctypes.addressof(raw) + toast.ToastMessage.strings.offset
        self.title = ctypes.cast(strings, ctypes.c_char_p).value
        self.content = ctypes.cast(strings+1+len(self.title), ctypes.c_char_p).value
        flags = yutani.WindowFlag.FLAG_NO_STEAL_FOCUS | yutani.WindowFlag.FLAG_DISALLOW_DRAG | yutani.WindowFlag.FLAG_DISALLOW_RESIZE
        super(Notification, self).__init__(self.base_width, self.base_height, flags=flags, doublebuffer=True)
        self.update_location(len(notifications) * (self.base_height + 8))

        self.title_tr = text_region.TextRegion(10,10,self.width-20,15,font=title_font)
        self.title_tr.set_text(self.title.decode('utf-8'))

        self.content_tr = text_region.TextRegion(10,30,self.width-20,self.height-40-5,font=content_font)
        self.content_tr.set_line_height(16)
        self.content_tr.set_richtext(self.content.decode('utf-8'))
        self.draw()

    def update_location(self, y):
        self.y = int(y)
        self.move(yutani.yutani_ctx._ptr.contents.display_width - self.base_width - 10, 35 + self.y)

    def slide_to(self, index, t):
        self.start = self.y
        self.start_time = t
        self.destination = index * (self.base_height + 8)
        if not self in sliding:
            sliding.append(self)

    def slide(self, time):
        if time - self.start_time > self.slide_time:
            self.update_location(self.destination)
            sliding.remove(self)
        else:
            self.update_location(self.start + min((time - self.start_time) / self.slide_time * (self.destination - self.start), 0))

    def draw(self):
        surface = self.get_cairo_surface()
        ctx = cairo.Context(surface)
        ctx.set_operator(cairo.OPERATOR_SOURCE)
        ctx.set_source_surface(background, 0, 0)
        ctx.paint()

        self.title_tr.draw(self)
        self.content_tr.draw(self)

        self.flip()

    def close(self):
        notifications.remove(self)
        if self in sliding:
            sliding.remove(self)
        super(Notification,self).close()


    def keyboard_event(self, msg):
        pass

    def mouse_event(self, msg):
        if msg.command == yutani.MouseEvent.CLICK or msg.command == yutani.MouseEvent.RAISE:
            self.close()
            return

def check_close():
    kill = []
    t = time.time()
    if sliding:
        for note in sliding:
            note.slide(t)
    for note in notifications:
        if note.ttl < t:
            kill.append(note)
    if kill:
        for note in kill:
            note.close()
        i = 0
        for note in notifications:
            note.slide_to(i, t)
            i += 1


if __name__ == '__main__':
    yutani.Yutani()

    daemon = 'toastd'
    if os.path.exists('/dev/pex/'+daemon):
        print(f"Toast daemon already running or /dev/pex/{daemon} not closed properly.")
        sys.exit(1)

    os.environ['TOASTD'] = daemon
    pex_server = pex.PexServer(daemon)

    fds = [yutani.yutani_ctx,pex_server]
    while 1:
        # Poll for events.
        fd = fswait.fswait(fds,20 if sliding else 500)

        if fd == 1:
            size, packet = pex_server.listen()
            if size and packet.size:
                data = ctypes.addressof(packet) + pex.pex_packet.data.offset
                notification = Notification(toast.ToastMessage.from_address(data))
                notifications.append(notification)
        check_close()
        while yutani.yutani_ctx.query():
            msg = yutani.yutani_ctx.poll()
            if not yutani_mainloop.handle_event(msg):
                os.unlink('/dev/pex/'+daemon)
                sys.exit(0)

