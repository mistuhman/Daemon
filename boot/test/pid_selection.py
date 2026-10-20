#!/usr/bin/env python3
"""Linux regression: argv mentions are not client identity; exact comm alone is not."""
import ctypes
import os
import subprocess
import sys
import time

boot = os.path.abspath(sys.argv[1])
children = []
try:
    # These used to outrank the actual client when its cgroup was absent.
    for argv in ('sober', 'org.vinegarhq.Sober', 'daemon/sober_probe'):
        children.append(subprocess.Popen([argv, '30'], executable='/bin/sleep'))
    output = subprocess.check_output([boot, '--check'], text=True)
    assert 'sober=0 pid=0 ' in output, output
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        ctypes.CDLL(None).prctl(15, b'sober', 0, 0, 0)
        os.write(w, b'1')
        os.close(w)
        time.sleep(30)
        os._exit(0)
    os.close(w)
    os.read(r, 1)
    os.close(r)
    try:
        output = subprocess.check_output([boot, '--check'], text=True)
        assert 'sober=0 pid=0 ' in output, output
    finally:
        os.kill(pid, 15)
        os.waitpid(pid, 0)
    print('PID_SELECTION_PASS: argv decoys rejected, unverified sober comm rejected')
finally:
    for child in children:
        child.terminate()
        child.wait()
