import ctypes
import importlib

_cairo_lib = None
_cairo_module = None
_cairo_module_lib = None

_lib = None

if not _lib:
    _lib = ctypes.CDLL('libtoaru_ext_freetype_fonts.so')
    #_lib.init_shmemfonts() # No init call in new library
    _lib.freetype_draw_string_width.argtypes = [ctypes.c_char_p]
    _lib.freetype_draw_string_width.restype = ctypes.c_uint32
    _lib.freetype_font_name.restype = ctypes.c_char_p
    _lib.freetype_draw_string.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_char_p]
    _lib.freetype_draw_string_shadow.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double]
    _lib.freetype_get_active_font_face.restype = ctypes.c_void_p

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

def get_active_font():
    return _lib.freetype_get_active_font_face()

def get_cairo_face():
    global _cairo_lib
    global _cairo_module
    global _cairo_module_lib
    if not _cairo_lib:
        _cairo_lib = ctypes.CDLL('libcairo.so')
        _cairo_module = importlib.import_module('_cairo')
        _cairo_module_lib = ctypes.CDLL(_cairo_module.__file__)

    cfffcfff = _cairo_lib.cairo_ft_font_face_create_for_ft_face
    cfffcfff.argtypes = [ctypes.c_void_p, ctypes.c_int]
    cfffcfff.restype = ctypes.c_void_p
    ft_face = cfffcfff(get_active_font(),0)

    pcfffff = _cairo_module_lib.PycairoFontFace_FromFontFace
    pcfffff.argtypes = [ctypes.c_void_p]
    pcfffff.restype = ctypes.py_object
    return pcfffff(ft_face)

class Font(object):

    def __init__(self, font_number, font_size=10, font_color=0xFF000000):
        self.font_number = font_number
        self.font_size = font_size
        self.font_color = font_color
        self.shadow = None

    def set_shadow(self, shadow):
        self.shadow = shadow

    def _use(self):
        _lib.freetype_set_font_face(self.font_number)
        _lib.freetype_set_font_size(self.font_size)

    def width(self, string):
        self._use()
        string = string.encode('utf-8')
        return _lib.freetype_draw_string_width(string)

    def write(self, ctx, x, y, string, shadow=None):
        if self.shadow:
            shadow = self.shadow
        self._use()
        foreground = self.font_color
        string = string.encode('utf-8')
        if hasattr(ctx,"_gfx"):
            # Allow a yutani.Window to be passed to this instead of a real graphics context
            ctx = ctx._gfx
        if shadow:
            color, darkness, offset_x, offset_y, radius = shadow
            _lib.freetype_draw_string_shadow(ctx,x,y,foreground,string,color,darkness,offset_x,offset_y,radius)
        else:
            _lib.freetype_draw_string(ctx,x,y,foreground,string)

    @property
    def name(self):
        return _lib.freetype_font_name(self.font_number).decode('utf-8')


