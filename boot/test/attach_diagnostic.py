#!/usr/bin/env python3
"""Exercise actual cross-process reads and ELF discovery, not mocked APIs."""
import ctypes
import mmap
import os
import pathlib
import shutil
import subprocess
import sys
import struct
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
# Permit this test's subprocess to read the fixture under Yama as well.
ctypes.CDLL(None).prctl(0x59616d61, ctypes.c_ulong(-1), 0, 0, 0)
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp) / 'libroblox.so'
    shutil.copy('/bin/true', path)
    with path.open('rb') as f:
        mapping = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        for args in (['--pid', str(os.getpid())], [], ['--window-pid', str(os.getpid())]):
            run = subprocess.run([binary, *args], capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            for expected in ('ATTACH_READ_ONLY=PASS', 'GNU_BUILD_ID=', 'ELF_LOAD_BIAS=', 'identity_stable=1'):
                assert expected in run.stdout, run.stdout
        # A loader need not retain the original library filename in maps.
        # Exercise both dynamic-pointer representations: the native loader
        # relocates DT_STRTAB; a synthetic mapped ELF keeps virtual addresses.
        native_src = pathlib.Path(tmp) / 'fixture.cpp'
        native_src.write_text('extern "C" int fixture() { return 42; }')
        native_so = pathlib.Path(tmp) / 'fixture.so'
        subprocess.run(['g++', '-shared', '-fPIC', str(native_src),
                        '-Wl,-soname,libfixture-native.so', '-o', str(native_so)], check=True)
        native = ctypes.CDLL(str(native_so))
        assert native.fixture() == 42
        image = bytearray(8192)
        ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
        struct.pack_into('<16sHHIQQQIHHHHHH', image, 0,
                         ident, 3, 62, 1, 0, 64, 0, 0, 64, 56, 2, 0, 0, 0)
        struct.pack_into('<IIQQQQQQ', image, 64, 1, 5, 0, 0, 0, 8192, 8192, 4096)
        struct.pack_into('<IIQQQQQQ', image, 120, 2, 4, 512, 512, 0, 64, 64, 8)
        for i, entry in enumerate(((5, 1024), (10, 64), (14, 1), (0, 0))):
            struct.pack_into('<qQ', image, 512 + i * 16, *entry)
        name = b'libfixture-relative.so\0'
        image[1025:1025 + len(name)] = name
        fd = os.memfd_create('')
        os.write(fd, image)
        anonymous_name = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
        run = subprocess.run([binary, '--pid', str(os.getpid())], capture_output=True, text=True)
        for expected in ('path=/memfd:', 'OTHER_ELF_PROBE',
                         'ELF_SONAME=libfixture-native.so', 'ELF_SONAME=libfixture-relative.so'):
            assert expected in run.stdout, run.stdout
        compact = subprocess.run([binary, '--images'], capture_output=True, text=True)
        assert 'ELF_SONAME=libfixture-relative.so' in compact.stdout, compact.stdout
        anonymous_name.close()
        os.close(fd)
        mapping.close()
    invalid = subprocess.run([binary, '--pid', 'HOST_PID'], capture_output=True, text=True)
    assert invalid.returncode == 2 and 'positive numeric value' in invalid.stderr
    bad = subprocess.run([binary, '--pid', '2147483647'], capture_output=True, text=True)
    assert bad.returncode == 1 and 'ATTACH_READ_ONLY=FAIL' in bad.stdout
    path.write_bytes(b'not an ELF module' * 128)
    with path.open('rb') as f:
        mapping = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        run = subprocess.run([binary, '--pid', str(os.getpid())], capture_output=True, text=True)
        assert run.returncode == 1 and 'ATTACH_READ_ONLY=FAIL' in run.stdout
        mapping.close()
print('ATTACH_DIAGNOSTIC_PASS: explicit/automatic real reads, window-PID matching, memfd inventory, ELF/build ID, vanished PID, invalid input/ELF, absolute/relative SONAME, compact mode')
