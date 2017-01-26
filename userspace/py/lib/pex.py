#!/usr/bin/python3
"""Toaru Packet EXchange bindings."""

import ctypes
import os

pex_lib = None
_libc = None

class pex_packet(ctypes.Structure):
    _fields_ = [
        ("source", ctypes.c_uint32),
        ("size",   ctypes.c_size_t),
        ("data",   ctypes.c_char*0),
    ]

class PexServer(object):

    def __init__(self, name):
        global pex_lib
        global _libc
        if not pex_lib:
            pex_lib = ctypes.CDLL("libtoaru-pex.so")
            _libc = ctypes.CDLL("libc.so")
            # FILE * pex_bind(int,char *)
            pex_lib.pex_bind.restype = ctypes.c_void_p
            pex_lib.pex_bind.argtypes = [ctypes.c_char_p]
            # size_t pex_listen(FILE *,pex_packet_t *)
            pex_lib.pex_listen.restype = ctypes.c_size_t
            pex_lib.pex_listen.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

        self.server = pex_lib.pex_bind(name.encode('utf-8'))
        self._fileno = _libc.fileno(self.server)

    def fileno(self):
        return self._fileno

    def listen(self):
        p = ctypes.create_string_buffer(1024+ctypes.sizeof(pex_packet))
        size = pex_lib.pex_listen(self.server, ctypes.byref(p))
        if not size:
            return (0, None)
        else:
            np = ctypes.cast(ctypes.addressof(p), ctypes.POINTER(pex_packet)).contents
            return (size, np)

