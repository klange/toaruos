import ctypes

_lib = None

if not _lib:
    _lib = ctypes.CDLL('libc.so')
    _lib.syscall_fswait.argtypes = [ctypes.c_int,ctypes.POINTER(ctypes.c_int)]
    _lib.syscall_fswait.restype = ctypes.c_int
    _lib.syscall_fswait2.argtypes = [ctypes.c_int,ctypes.POINTER(ctypes.c_int),ctypes.c_int]
    _lib.syscall_fswait2.restype = ctypes.c_int

def fswait(files,timeout=None):
    fds = (ctypes.c_int * len(files))()
    for i in range(len(files)):
        fds[i] = files[i].fileno()
    if timeout is None:
        return _lib.syscall_fswait(len(fds),fds)
    else:
        return _lib.syscall_fswait2(len(fds),fds,timeout)

