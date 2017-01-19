#!/usr/bin/python3
"""
Bindings for the Yutani graphics libraries, including the core Yutani protocol,
general graphics routines, and the system decoration library.
"""

from ctypes import *

yutani_lib = None
yutani_gfx_lib = None
yutani_ctx = None
yutani_windows = {}

def usleep(microseconds):
    CDLL('libc.so').usleep(microseconds)

class Message(object):
    """A generic event message from the Yutani server."""
    class _yutani_msg_t(Structure):
        _fields_ = [
            ('magic', c_uint32),
            ('type', c_uint32),
            ('size', c_uint32),
            ('data', c_char*0),
        ]

    MSG_HELLO               = 0x00000001
    MSG_WINDOW_NEW          = 0x00000002
    MSG_FLIP                = 0x00000003
    MSG_KEY_EVENT           = 0x00000004
    MSG_MOUSE_EVENT         = 0x00000005
    MSG_WINDOW_MOVE         = 0x00000006
    MSG_WINDOW_CLOSE        = 0x00000007
    MSG_WINDOW_SHOW         = 0x00000008
    MSG_WINDOW_HIDE         = 0x00000009
    MSG_WINDOW_STACK        = 0x0000000A
    MSG_WINDOW_FOCUS_CHANGE = 0x0000000B
    MSG_WINDOW_MOUSE_EVENT  = 0x0000000C
    MSG_FLIP_REGION         = 0x0000000D
    MSG_WINDOW_NEW_FLAGS    = 0x0000000E
    MSG_RESIZE_REQUEST      = 0x00000010
    MSG_RESIZE_OFFER        = 0x00000011
    MSG_RESIZE_ACCEPT       = 0x00000012
    MSG_RESIZE_BUFID        = 0x00000013
    MSG_RESIZE_DONE         = 0x00000014
    MSG_WINDOW_ADVERTISE    = 0x00000020
    MSG_SUBSCRIBE           = 0x00000021
    MSG_UNSUBSCRIBE         = 0x00000022
    MSG_NOTIFY              = 0x00000023
    MSG_QUERY_WINDOWS       = 0x00000024
    MSG_WINDOW_FOCUS        = 0x00000025
    MSG_WINDOW_DRAG_START   = 0x00000026
    MSG_WINDOW_WARP_MOUSE   = 0x00000027
    MSG_WINDOW_SHOW_MOUSE   = 0x00000028
    MSG_WINDOW_RESIZE_START = 0x00000029
    MSG_SESSION_END         = 0x00000030
    MSG_KEY_BIND            = 0x00000040
    MSG_WINDOW_UPDATE_SHAPE = 0x00000050
    MSG_GOODBYE             = 0x000000F0
    MSG_TIMER_REQUEST       = 0x00000100
    MSG_TIMER_TICK          = 0x00000101
    MSG_WELCOME             = 0x00010001
    MSG_WINDOW_INIT         = 0x00010002

    def __init__(self, msg):
        self._ptr = msg

    @property
    def type(self):
        return self._ptr.contents.type

_message_types = {}

class MessageBuilder(type):

    def __new__(cls, name, bases, dct):
        global _message_types
        new_cls = super(MessageBuilder, cls).__new__(cls, name, bases, dct)
        if 'type_val' in dct:
            _message_types[dct['type_val']] = new_cls
        return new_cls

class MessageEx(Message, metaclass=MessageBuilder):
    """An event message with extra data available."""
    type_val = None
    data_struct = None

    def __init__(self, msg):
        Message.__init__(self, msg)
        self._data_ptr = cast(byref(self._ptr.contents,Message._yutani_msg_t.data.offset), POINTER(self.data_struct))

    def __getattr__(self, name):
        if name in dir(self._data_ptr.contents):
            return getattr(self._data_ptr.contents, name)
        raise AttributeError(name)

class MessageWelcome(MessageEx):
    """Message sent by the server on display size changes."""
    type_val = Message.MSG_WELCOME
    class data_struct(Structure):
        _fields_ = [
            ('display_width', c_uint32),
            ('display_height', c_uint32),
        ]

class Keycode(object):
    """Keycodes."""
    NONE        = 0
    BACKSPACE   = 8
    CTRL_A      = 1
    CTRL_B      = 2
    CTRL_C      = 3
    CTRL_D      = 4
    CTRL_E      = 5
    CTRL_F      = 6
    CTRL_G      = 7
    CTRL_H      = 8
    CTRL_I      = 9
    CTRL_J      = 10
    CTRL_K      = 11
    CTRL_L      = 12
    CTRL_M      = 13
    CTRL_N      = 14
    CTRL_O      = 15
    CTRL_P      = 16
    CTRL_Q      = 17
    CTRL_R      = 18
    CTRL_S      = 19
    CTRL_T      = 20
    CTRL_U      = 21
    CTRL_V      = 22
    CTRL_W      = 23
    CTRL_X      = 24
    CTRL_Y      = 25
    CTRL_Z      = 26
    ESCAPE      = 27
    NORMAL_MAX  = 256
    ARROW_UP    = 257
    ARROW_DOWN  = 258
    ARROW_RIGHT = 259
    ARROW_LEFT  = 260
    BAD_STATE   = -1

    CTRL_ARROW_UP    = 261
    CTRL_ARROW_DOWN  = 262
    CTRL_ARROW_RIGHT = 263
    CTRL_ARROW_LEFT  = 264

    SHIFT_ARROW_UP    = 265
    SHIFT_ARROW_DOWN  = 266
    SHIFT_ARROW_RIGHT = 267
    SHIFT_ARROW_LEFT  = 268

    LEFT_CTRL   = 1001
    LEFT_SHIFT  = 1002
    LEFT_ALT    = 1003
    LEFT_SUPER  = 1004

    RIGHT_CTRL  = 1011
    RIGHT_SHIFT = 1012
    RIGHT_ALT   = 1013
    RIGHT_SUPER = 1014

    F1          = 2001
    F2          = 2002
    F3          = 2003
    F4          = 2004
    F5          = 2005
    F6          = 2006
    F7          = 2007
    F8          = 2008
    F9          = 2009
    F10         = 2010
    F11         = 2011
    F12         = 2012

    PAGE_DOWN   = 2013
    PAGE_UP     = 2014

    HOME        = 2015
    END         = 2016
    DEL         = 2017
    INSERT      = 2018

class Modifier(object):
    """Modifier key flags."""
    MOD_LEFT_CTRL   = 0x01
    MOD_LEFT_SHIFT  = 0x02
    MOD_LEFT_ALT    = 0x04
    MOD_LEFT_SUPER  = 0x08
    MOD_RIGHT_CTRL  = 0x10
    MOD_RIGHT_SHIFT = 0x20
    MOD_RIGHT_ALT   = 0x40
    MOD_RIGHT_SUPER = 0x80

class KeyAction(object):
    """Keyboard action (up or down)"""
    ACTION_DOWN = 1
    ACTION_UP = 2

class MessageKeyEvent(MessageEx):
    """Message containing key event information."""
    type_val = Message.MSG_KEY_EVENT
    class data_struct(Structure):
        class key_event_t(Structure):
            _fields_ = [
                ('keycode', c_uint),
                ('modifiers', c_uint),
                ('action', c_ubyte),
                ('key', c_char),
            ]
        class key_event_state_t(Structure):
            _fields = [
                ("kbd_state", c_int),
                ("kbd_s_state", c_int),

                ("k_ctrl", c_int),
                ("k_shift", c_int),
                ("k_alt", c_int),
                ("k_super", c_int),

                ("kl_ctrl", c_int),
                ("kl_shift", c_int),
                ("kl_alt", c_int),
                ("kl_super", c_int),

                ("kr_ctrl", c_int),
                ("kr_shift", c_int),
                ("kr_alt", c_int),
                ("kr_super", c_int),

                ("kbd_esc_buf", c_int),
            ]
        _fields_ = [
            ('wid', c_uint32),
            ('event', key_event_t),
            ('state', key_event_state_t),
        ]

class MessageWindowMouseEvent(MessageEx):
    """Message containing window-relative mouse event information."""
    type_val = Message.MSG_WINDOW_MOUSE_EVENT
    class data_struct(Structure):
        _fields_ = [
            ('wid', c_uint32),
            ('new_x', c_int32),
            ('new_y', c_int32),
            ('old_x', c_int32),
            ('old_y', c_int32),
            ('buttons', c_ubyte),
            ('command', c_ubyte),
        ]

class MessageWindowFocusChange(MessageEx):
    """Message indicating the focus state of a window has changed."""
    type_val = Message.MSG_WINDOW_FOCUS_CHANGE
    class data_struct(Structure):
        _fields_ = [
            ('wid', c_uint32),
            ('focused', c_int),
        ]

class MessageWindowResize(MessageEx):
    """Message indicating the server wishes to resize this window."""
    type_val = Message.MSG_RESIZE_OFFER
    class data_struct(Structure):
        _fields_ = [
            ('wid', c_uint32),
            ('width', c_uint32),
            ('height', c_uint32),
            ('bufid', c_uint32),
        ]

class MessageWindowAdvertisement(MessageEx):
    """Message containing information about a foreign window."""
    type_val = Message.MSG_WINDOW_ADVERTISE
    class data_struct(Structure):
        _fields_ = [
            ('wid', c_uint32),
            ('flags', c_uint32),
            ('offsets', c_uint16 * 5),
            ('size', c_uint32),
            ('strings', c_byte * 0),
        ]

    @property
    def name(self):
        return string_at(addressof(self.strings) + self.offsets[0]).decode('utf-8')

    @property
    def icon(self):
        return string_at(addressof(self.strings) + self.offsets[1]).decode('utf-8')

class MessageWindowMove(MessageEx):
    """Message received when a window has moved containing its new coordinates."""
    type_val = Message.MSG_WINDOW_MOVE
    class data_struct(Structure):
        _fields_ = [
            ('wid', c_uint32),
            ('x', c_int32),
            ('y', c_int32),
        ]

class Yutani(object):
    """Base Yutani communication class. Must be initialized to start a connection."""
    class _yutani_t(Structure):
        _fields_ = [
            ("sock", c_void_p), # File pointer
            ("display_width", c_size_t),
            ("display_height", c_size_t),
            ("windows", c_void_p), # hashmap
            ("queued", c_void_p), # list
            ("server_ident", c_char_p),
        ]

    def __init__(self):
        global yutani_lib
        global yutani_ctx
        global yutani_gfx_lib
        yutani_lib = CDLL("libtoaru-yutani.so")
        yutani_gfx_lib = CDLL("libtoaru-graphics.so")
        self._ptr = cast(yutani_lib.yutani_init(), POINTER(self._yutani_t))
        yutani_ctx = self
        self._fileno = CDLL('libc.so').fileno(self._ptr.contents.sock)

    def poll(self, sync=True):
        """Poll for an event message."""
        if sync:
            result = yutani_lib.yutani_poll(self._ptr)
        else:
            result = yutani_lib.yutani_poll_async(self._ptr)
        if not result:
            return None
        msg_ptr = cast(result, POINTER(Message._yutani_msg_t))
        msg_class = _message_types.get(msg_ptr.contents.type, Message)
        return msg_class(msg_ptr)

    def wait_for(self, message):
        """Wait for a particular kind of message to be delivered."""
        result = yutani_lib.yutani_wait_for(self._ptr, message)
        msg_ptr = cast(result, POINTER(Message._yutani_msg_t))
        msg_class = _message_types.get(msg_ptr.contents.type, Message)
        return msg_class(msg_ptr)

    def subscribe(self):
        """Subscribe to window information changes."""
        yutani_lib.yutani_subscribe_windows(self._ptr)

    def unsubscribe(self):
        """Unsubscribe from window information changes."""
        yutani_lib.yutani_unsubscribe_windows(self._ptr)

    def query_windows(self):
        """Request a window subsription list."""
        yutani_lib.yutani_query_windows(self._ptr)

    def timer_request(self, precision=0, flags=0):
        """Request timer tick messages."""
        yutani_lib.yutani_timer_request(self._ptr, precision, flags)

    def focus_window(self, wid):
        """Request that the server change the focused window to the window with the specified wid."""
        yutani_lib.yutani_focus_window(self._ptr, wid)

    def session_end(self):
        """Request the end of the user session."""
        yutani_lib.yutani_session_end(self._ptr)

    def key_bind(self, keycode, modifiers, flags):
        """Set global key binding."""
        yutani_lib.yutani_key_bind(self._ptr, keycode, modifiers, flags)

    def fileno(self):
        """Act file-like and return our file descriptor number."""
        return self._fileno

class KeybindFlag(object):
    """Flags for global key bindings."""
    BIND_PASSTHROUGH = 0 # The key bind should be received by other clients.
    BIND_STEAL       = 1 # The key bind should stop after being processed here.

class WindowShape(object):
    """Window shaping modes for Window.update_shape."""
    # These are actually values representing the minimum required
    # alpha value for a pixel to be registered as part of the given window.
    # 256 is more than the maximum alpha value, so all clicks will pass through.
    # 0 will catch every pixel, even if it is entirely transparent.
    THRESHOLD_NONE        = 0
    THRESHOLD_CLEAR       = 1
    THRESHOLD_HALF        = 127
    THRESHOLD_ANY         = 255
    THRESHOLD_PASSTHROUGH = 256

class WindowStackOrder(object):
    """Window stack order options."""
    ZORDER_MAX    = 0xFFFF
    ZORDER_TOP    = 0xFFFF
    ZORDER_BOTTOM = 0x0000

class WindowFlag(object):
    """Flags for window creation."""
    FLAG_NO_STEAL_FOCUS  = (1 << 0) # Don't steal focus on window creation.
    FLAG_DISALLOW_DRAG   = (1 << 1) # Don't allow this window to be dragged.
    FLAG_DISALLOW_RESIZE = (1 << 2) # Don't allow this window to be resized.
    FLAG_ALT_ANIMATION   = (1 << 3) # Use the alternate animation when mapping and unmapping.

class MouseButton(object):
    """Mouse button flags."""
    BUTTON_LEFT   = 0x01
    BUTTON_RIGHT  = 0x02
    BUTTON_MIDDLE = 0x04
    SCROLL_UP     = 0x10
    SCROLL_DOWN   = 0x20

class MouseEvent(object):
    """Mouse event types."""
    CLICK = 0
    DRAG  = 1
    RAISE = 2
    DOWN  = 3
    MOVE  = 4
    LEAVE = 5
    ENTER = 6

class CursorType(object):
    """Cursor types for show_mouse."""
    RESET             = -1
    HIDE              = 0
    NORMAL            = 1
    DRAG              = 2
    RESIZE_VERTICAL   = 3
    RESIZE_HORIZONTAL = 4
    RESIZE_UP_DOWN    = 5
    RESIZE_DOWN_UP    = 6

class GraphicsBuffer(object):
    """Generic buffer for rendering."""

    def __init__(self, width, height):
        self.width = width
        self.height = height

        self._sprite = yutani_gfx_lib.create_sprite(width,height,2)
        self._gfx = cast(yutani_gfx_lib.init_graphics_sprite(self._sprite),POINTER(Window._gfx_context_t))

    def get_cairo_surface(self):
        return Window.get_cairo_surface(self)

    def get_value(self,x,y):
        return cast(self._gfx.contents.backbuffer,POINTER(c_uint32))[self.width * y + x]

    def destroy(self):
        yutani_gfx_lib.sprite_free(self._sprite)
        CDLL('libc.so').free(self._gfx)


class Window(object):
    """Yutani Window object."""
    class _yutani_window_t(Structure):
        _fields_ = [
            ("wid", c_uint),
            ("width", c_uint32),
            ("height", c_uint32),
            ("buffer", POINTER(c_uint8)),
            ("bufid", c_uint32),
            ("focused", c_uint8),
            ("oldbufid", c_uint32),
        ]

    class _gfx_context_t(Structure):
        _fields_ = [
            ('width', c_uint16),
            ('height', c_uint16),
            ('depth', c_uint16),
            ('size', c_uint32),
            ('buffer', POINTER(c_char)),
            ('backbuffer', POINTER(c_char)),
        ]

    def __init__(self, width, height, flags=0, title=None, icon=None, doublebuffer=False):
        if not yutani_ctx:
            raise ValueError("Not connected.")

        self._ptr = cast(yutani_lib.yutani_window_create_flags(yutani_ctx._ptr, width, height, flags), POINTER(self._yutani_window_t))

        yutani_windows[self.wid] = self

        self.doublebuffer = doublebuffer

        if doublebuffer:
            self._gfx = cast(yutani_lib.init_graphics_yutani_double_buffer(self._ptr), POINTER(self._gfx_context_t))
        else:
            self._gfx = cast(yutani_lib.init_graphics_yutani(self._ptr), POINTER(self._gfx_context_t))

        if title:
            self.set_title(title, icon)

        self.closed = False

    def get_cairo_surface(self):
        """Obtain a pycairo.ImageSurface representing the window backbuffer."""
        import _cairo
        buffer = self._gfx.contents.backbuffer
        width = self.width
        height = self.height
        format = _cairo.FORMAT_ARGB32
        # int cairo_format_stride_for_width(cairo_format_t format, int width)
        cfsfw = CDLL('libcairo.so').cairo_format_stride_for_width
        cfsfw.argtypes = [c_int, c_int]
        cfsfw.restype = c_int
        # stride = cairo_format_stride_for_width(format, width)
        stride = cfsfw(format, width)
        # cairo_surface_t * cairo_image_surface_create_for_data(unsigned char * data, cairo_format_t format, int ...)
        ciscfd = CDLL('libcairo.so').cairo_image_surface_create_for_data
        ciscfd.argtypes = [POINTER(c_char), c_int, c_int, c_int, c_int]
        ciscfd.restype = c_void_p
        # surface = cairo_image_surface_create_for_data(buffer,format,width,height,stride)
        surface = ciscfd(buffer,format,width,height,stride)
        # PyObject * PycairoSurface_FromSurface(cairo_surface_t * surface, PyObject * base)
        pcsfs = CDLL(_cairo.__file__).PycairoSurface_FromSurface
        pcsfs.argtypes = [c_void_p, c_int]
        pcsfs.restype = py_object
        # return PycairoSurface_FromSurface(surface, NULL)
        return pcsfs(surface, 0)


    def set_title(self, title, icon=None):
        """Advertise this window with the given title and optional icon string."""
        self.title = title
        self.icon = icon
        title_string = title.encode('utf-8') if title else None
        icon_string = icon.encode('utf-8') if icon else None

        if not icon:
            yutani_lib.yutani_window_advertise(yutani_ctx._ptr, self._ptr, title_string)
        else:
            yutani_lib.yutani_window_advertise_icon(yutani_ctx._ptr, self._ptr, title_string, icon_string)

    def buffer(self):
        """Obtain a reference to the graphics backbuffer representing this window's canvas."""
        return cast(self._gfx.contents.backbuffer, POINTER(c_uint32))

    def flip(self, region=None):
        """Flip the window buffer when double buffered and inform the server of updates."""
        if self.doublebuffer:
            yutani_gfx_lib.flip(self._gfx)
        yutani_lib.yutani_flip(yutani_ctx._ptr, self._ptr)

    def close(self):
        """Close the window."""
        if self.wid in yutani_windows:
            del yutani_windows[self.wid]
        self.closed = True
        yutani_lib.yutani_close(yutani_ctx._ptr, self._ptr)
        yutani_lib.release_graphics_yutani(self._gfx)

    def move(self, x, y):
        """Move the window to the requested location."""
        yutani_lib.yutani_window_move(yutani_ctx._ptr, self._ptr, x, y)

    def resize_accept(self, w, h):
        """Inform the server that we have accepted the offered resize."""
        yutani_lib.yutani_window_resize_accept(yutani_ctx._ptr, self._ptr, w, h)

    def resize_done(self):
        """Inform the server that we are done resizing and the new window may be displayed."""
        yutani_lib.yutani_window_resize_done(yutani_ctx._ptr, self._ptr)

    def resize_offer(self, width, height):
        """Offer alternative dimensions in response to a server offer."""
        yutani_lib.yutani_window_resize_offer(yutani_ctx._ptr, self._ptr, width, height)

    def resize(self, width, height):
        """Request that a window be resized to the given dimensions."""
        yutani_lib.yutani_window_resize(yutani_ctx._ptr, self._ptr, width, height)

    def reinit(self):
        """Reinitialize the internal graphics context for the window. Should be done after a resize_accept."""
        yutani_lib.reinit_graphics_yutani(self._gfx, self._ptr)

    def fill(self, color):
        """Fill the entire window with a given color."""
        yutani_gfx_lib.draw_fill(self._gfx, color)

    def update_shape(self, mode):
        """Set the mouse passthrough / window shaping mode. Does not affect appearance of window."""
        yutani_lib.yutani_window_update_shape(yutani_ctx._ptr, self._ptr, mode)

    def set_stack(self, stack):
        """Set the stack layer for the window."""
        yutani_lib.yutani_set_stack(yutani_ctx._ptr, self._ptr, stack)

    @property
    def width(self):
        return self._ptr.contents.width

    @property
    def height(self):
        return self._ptr.contents.height

    # TODO: setters for width/height call resize?

    @property
    def wid(self):
        """The identifier of the window."""
        return self._ptr.contents.wid

    @property
    def focused(self):
        """Whether the window is current focused."""
        return self._ptr.contents.focused

    @focused.setter
    def focused(self, value):
        self._ptr.contents.focused = value

class Decor(object):
    """Class for rendering decorations with the system decorator library."""

    EVENT_OTHER = 1
    EVENT_CLOSE = 2
    EVENT_RESIZE = 3

    def __init__(self):
        self.lib = CDLL("libtoaru-decorations.so")
        self.lib.init_decorations()

    def width(self):
        """The complete width of the left and right window borders."""
        return int(self.lib.decor_width())

    def height(self):
        """The complete height of the top and bottom window borders."""
        return int(self.lib.decor_height())

    def top_height(self):
        """The height of the top edge of the decorations."""
        return c_uint32.in_dll(self.lib, "decor_top_height").value

    def bottom_height(self):
        """The height of the bottom edge of the decorations."""
        return c_uint32.in_dll(self.lib, "decor_bottom_height").value

    def left_width(self):
        """The width of the left edge of the decorations."""
        return c_uint32.in_dll(self.lib, "decor_left_width").value

    def right_width(self):
        """The width of the right edge of the decorations."""
        return c_uint32.in_dll(self.lib, "decor_right_width").value

    def render(self, window, title=None):
        """Render decorations on this window. If a title is not provided, it will be retreived from the window object."""
        if not title:
            title = window.title
        title_string = title.encode('utf-8') if title else None
        self.lib.render_decorations(window._ptr, window._gfx, title_string)

    def handle_event(self, msg):
        """Let the decorator library handle an event. Usually passed mouse events."""
        return self.lib.decor_handle_event(yutani_ctx._ptr, msg._ptr)

# Demo follows.
if __name__ == '__main__':
    # Connect to the server.
    Yutani()

    # Initialize the decoration library.
    d = Decor()

    # Create a new window.
    w = Window(200+d.width(),200+d.height(),title="Python Demo")

    # Since this is Python, we can attach data to our window, such
    # as its internal width (excluding the decorations).
    w.int_width = 200
    w.int_height = 200

    # We can set window shaping...
    w.update_shape(WindowShape.THRESHOLD_HALF)

    # Move the window...
    w.move(100, 100)

    def draw_decors():
        """Render decorations for the window."""
        d.render(w)

    def draw_window():
        """Draw the window."""
        w.fill(0xFFFF00FF)
        draw_decors()

    def finish_resize(msg):
        """Accept a resize."""

        # Tell the server we accept.
        w.resize_accept(msg.width, msg.height)

        # Reinitialize internal graphics context.
        w.reinit()

        # Calculate new internal dimensions.
        w.int_width = msg.width - d.width()
        w.int_height = msg.height - d.height()

        # Redraw the window buffer.
        draw_window()

        # Inform the server we are done.
        w.resize_done()

        # And flip.
        w.flip()

    # Do an initial draw.
    draw_window()

    # Don't forget to flip. Our single-buffered window only needs
    # the Yutani flip call, but the API will perform both if needed.
    w.flip()

    while 1:
        # Poll for events.
        msg = yutani_ctx.poll()
        if msg.type == Message.MSG_SESSION_END:
            # All applications should attempt to exit on SESSION_END.
            w.close()
            break
        elif msg.type == Message.MSG_KEY_EVENT:
            # Print key events for debugging.
            print(f'W({msg.wid}) key {msg.event.key} {msg.event.action}')
            if msg.event.key == b'q':
                # Convention says to close windows when 'q' is pressed,
                # unless we're using keyboard input "normally".
                w.close()
                break
        elif msg.type == Message.MSG_WINDOW_FOCUS_CHANGE:
            # If the focus of our window changes, redraw the borders.
            if msg.wid == w.wid:
                # This attribute is stored in the underlying struct
                # and used by the decoration library to pick which
                # version of the decorations to draw for the window.
                w.focused = msg.focused
                draw_decors()
                w.flip()
        elif msg.type == Message.MSG_RESIZE_OFFER:
            # Resize the window.
            finish_resize(msg)
        elif msg.type == Message.MSG_WINDOW_MOUSE_EVENT:
            # Handle mouse events, first by passing them
            # to the decorator library for processing.
            if d.handle_event(msg) == Decor.EVENT_CLOSE:
                # Close the window when the 'X' button is clicked.
                w.close()
                break
            else:
                # For events that didn't get handled by the decorations,
                # print a debug message with details.
                print(f'W({msg.wid}) mouse {msg.new_x},{msg.new_y}')
