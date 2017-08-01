"""
Small WebP library
"""
import os
import cairo
import ctypes

_webp_lib = None

WebPGetInfo = None
WebPDecodeBGRAInto = None

def exists():
    return os.path.exists('/usr/lib/libwebp.so')

def load_webp(path):
    global _webp_lib
    global WebPGetInfo
    global WebPDecodeBGRAInto

    if not exists():
        return None

    if not _webp_lib:
        _webp_lib = ctypes.CDLL('libwebp.so')
        WebPGetInfo = _webp_lib.WebPGetInfo
        WebPGetInfo.argtypes = [ctypes.POINTER(ctypes.c_char), ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        WebPGetInfo.restype = ctypes.c_int

        WebPDecodeBGRAInto = _webp_lib.WebPDecodeBGRAInto
        WebPDecodeBGRAInto.argtypes = [ctypes.POINTER(ctypes.c_char), ctypes.c_int, ctypes.POINTER(ctypes.c_char), ctypes.c_int, ctypes.c_int]
        WebPDecodeBGRAInto.restype = ctypes.c_int

    with open(path, 'rb') as f:
        data = f.read()

    _width = ctypes.c_int()
    _height = ctypes.c_int()
    WebPGetInfo(data, len(data), ctypes.byref(_width), ctypes.byref(_height))
    width, height = _width.value, _height.value

    buf = ctypes.create_string_buffer(b'\000' * width * height * 4)
    WebPDecodeBGRAInto(data, len(data), buf, width * height * 4, width * 4)

    return cairo.ImageSurface.create_for_data(buf, cairo.FORMAT_ARGB32, width, height, width * 4)
