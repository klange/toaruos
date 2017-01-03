#!/usr/bin/python3

import yutani

decorations = None
_windows = {}

ttk_lib = None

def init_ttk():
    global ttk_lib
    from ctypes import CDLL
    ttk_lib = CDLL("libtoaru-ttk.so")

def rgb(r,g,b):
    return yutani.yutani_gfx_lib.rgb(r,g,b)

class Button(object): # TODO widget base class?
    pass

class Window(object): # TODO container base class?

    def __init__(self):
        global decorations

        if not yutani.yutani_lib:
            yutani.Yutani()

        if not decorations:
            decorations = yutani.Decor()

        self.decorated = True
        self._win = None
        self.title = "TTK Window"

    def _create_window(self):
        w,h = self._calculate_bounds()
        self._win = yutani.Window(w,h, flags=0, title=self.title)
        self._win.move(100, 100)
        _windows[self._win.wid] = self

    def _calculate_bounds(self):
        return (decorations.width() + 200, decorations.height() + 200)

    def show(self):
        if not self._win:
            self._create_window()
        self._win.fill(rgb(214,214,214))
        if self.decorated:
            decorations.render(self._win, self.title)
        self._win.flip()

    def close(self):
        # TODO callback
        self._win.close()

def main():
    """TTK main"""
    while 1:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        if msg.type == yutani.Message.MSG_SESSION_END:
            # All applications should attempt to exit on SESSION_END.
            for w in _windows.values():
                w.close()
            break
        elif msg.type == yutani.Message.MSG_KEY_EVENT:
            # Print key events for debugging.
            if msg.event.key == b'q':
                # Convention says to close windows when 'q' is pressed,
                # unless we're using keyboard input "normally".
                w = _windows.get(msg.wid)
                if w:
                    w.close()
                    break
        elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
            # If the focus of our window changes, redraw the borders.
            w = _windows.get(msg.wid)
            if w:
                w._win.focused = msg.focused
                w.show()
        elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
            # Resize request for window.
            pass
        elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
            w = _windows.get(msg.wid)
            if w:
                r = None
                if w.decorated:
                    r = decorations.handle_event(msg)
                if r == yutani.Decor.EVENT_CLOSE:
                    w.close
                    break
                else:
                    pass

init_ttk()

if __name__ == '__main__':
    w = Window()
    w.show()
    main()
