#!/usr/bin/env python3
"""End-to-end production scanner test with real unnamed memfd reads."""
import ctypes
import mmap
import os
import pathlib
import signal
import struct
import subprocess
import sys

binary = str(pathlib.Path(sys.argv[1]).resolve())
ctypes.CDLL(None).prctl(0x59616d61, ctypes.c_ulong(-1), 0, 0, 0)
def scan():
    return subprocess.check_output([binary], text=True).strip()
assert scan() == 'pid=0 base=0'
# Exact sober comm is no longer sufficient evidence.
ctypes.CDLL(None).prctl(15, b'sober', 0, 0, 0)
assert scan() == 'pid=0 base=0'
ctypes.CDLL(None).prctl(15, b'Main', 0, 0, 0)
image = bytearray(8192)
ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
struct.pack_into('<16sHHIQQQIHHHHHH', image, 0,
                 ident, 3, 62, 1, 0, 64, 0, 0, 64, 56, 2, 0, 0, 0)
struct.pack_into('<IIQQQQQQ', image, 64, 1, 5, 0, 0, 0, 8192, 8192, 4096)
struct.pack_into('<IIQQQQQQ', image, 120, 2, 4, 512, 512, 0, 64, 64, 8)
for i, entry in enumerate(((5, 1024), (10, 64), (14, 1), (0, 0))):
    struct.pack_into('<qQ', image, 512 + i * 16, *entry)
image[1025:1038] = b'libroblox.so\0'  # preserve bytearray length below
# Slice above is 13 bytes including NUL.
fd = os.memfd_create('')
os.write(fd, image)
mem = mmap.mmap(fd, len(image), prot=mmap.PROT_READ | mmap.PROT_WRITE)
buf = ctypes.c_char.from_buffer(mem)
address = ctypes.addressof(buf)
assert scan() == 'pid=0 base=0', 'data-only ELF must not identify a game'
libc = ctypes.CDLL(None)
libc.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
assert libc.mprotect(address, len(mem), mmap.PROT_READ | mmap.PROT_EXEC) == 0
assert scan() == f'pid={os.getpid()} base={address}'
# A second process with the same image is ambiguous, never ranked by PID.
r, w = os.pipe()
child = os.fork()
if child == 0:
    os.close(r)
    libc.prctl(0x59616d61, ctypes.c_ulong(-1), 0, 0, 0)
    os.write(w, b'1')
    os.close(w)
    signal.pause()
    os._exit(0)
os.close(w)
os.read(r, 1)
os.close(r)
try:
    assert scan() == 'pid=0 base=0', 'ambiguous processes must fail closed'
finally:
    os.kill(child, signal.SIGTERM)
    os.waitpid(child, 0)
assert scan() == f'pid={os.getpid()} base={address}'
del buf
mem.close()
os.close(fd)
assert scan() == 'pid=0 base=0', 'unloaded image must not stay cached'
print('VERIFIED_SELECTION_PASS: reject comm/data-only, select Main unnamed ELF, exact bias, reject ambiguity, rediscover after unload')
