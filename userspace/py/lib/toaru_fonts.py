import ctypes

_lib = None

if not _lib:
    _lib = ctypes.CDLL('libtoaru-shmemfonts.so')
    _lib.init_shmemfonts()
    _lib.draw_string_width.argtypes = [ctypes.c_char_p]
    _lib.draw_string_width.restype = ctypes.c_uint32
    _lib.shmem_font_name.restype = ctypes.c_char_p
    _lib.draw_string.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_char_p]
    _lib.draw_string_shadow.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double]

FONT_SANS_SERIF             = 0
FONT_SANS_SERIF_BOLD        = 1
FONT_SANS_SERIF_ITALIC      = 2
FONT_SANS_SERIF_BOLD_ITALIC = 3
FONT_MONOSPACE              = 4
FONT_MONOSPACE_BOLD         = 5
FONT_MONOSPACE_ITALIC       = 6
FONT_MONOSPACE_BOLD_ITALIC  = 7
FONT_JAPANESE               = 8
FONT_SYMBOLA                = 9

class Font(object):

    def __init__(self, font_number, font_size=10, font_color=0xFF000000):
        self.font_number = font_number
        self.font_size = font_size
        self.font_color = font_color

    def _use(self):
        _lib.set_font_face(self.font_number)
        _lib.set_font_size(self.font_size)

    def width(self, string):
        self._use()
        string = string.encode('utf-8')
        return _lib.draw_string_width(string)

    def write(self, ctx, x, y, string, shadow=None):
        self._use()
        foreground = self.font_color
        string = string.encode('utf-8')
        if hasattr(ctx,"_gfx"):
            # Allow a yutani.Window to be passed to this instead of a real graphics context
            ctx = ctx._gfx
        if shadow:
            color, darkness, offset_x, offset_y, radius = shadow
            _lib.draw_string_shadow(ctx,x,y,foreground,string,color,darkness,offset_x,offset_y,radius)
        else:
            _lib.draw_string(ctx,x,y,foreground,string)

    @property
    def name(self):
        return _lib.shmem_font_name(self.font_number).decode('utf-8')

