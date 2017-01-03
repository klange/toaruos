#!/usr/bin/python3
"""ToaruOS toast library.

Sends messages to the notification daemon for display."""

import os
from ctypes import *

pex_lib = CDLL("libtoaru-pex.so")
pex_conn = None

class ToastMessage(Structure):
    _fields_ = [
        ("ttl", c_uint),
        ("strings", c_char * 0),
    ]

def init_toast():
    """Initialize the connection to the toast daemon. This should happen automatically."""
    global pex_conn
    server = os.environ.get("TOASTD","toastd").encode('utf-8')
    pex_conn = pex_lib.pex_connect(server)

def send_toast(title, message, ttl=5):
    """Send a toast message to the daemon."""

    # If not yet connected, connect.
    if not pex_conn:
        init_toast()

    # Title and message need to be C strings.
    title = title.encode('utf-8')
    message = message.encode('utf-8')

    # Build message struct.
    s = len(title) + len(message) + 2
    b = create_string_buffer(s + sizeof(ToastMessage))
    m = ToastMessage(ttl=ttl)
    memmove(addressof(b), addressof(m), sizeof(m))
    memmove(addressof(b)+sizeof(m),title,len(title)+1)
    memmove(addressof(b)+sizeof(m)+len(title)+1,message,len(message)+1)

    # Send it off.
    pex_lib.pex_reply(pex_conn, s + sizeof(ToastMessage), b)


