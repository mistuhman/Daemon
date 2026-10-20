#!/usr/bin/env python3
"""Turn raw soft-raster renders into the committed proof shots.

    python3 tools/encode_shots.py shots   build/shots           -> out/
    python3 tools/encode_shots.py menu    build/menuproof       -> preview/img/menu/

The headless suites write PNGs that ImageMagick deflates badly (about 5 MB each for
the 1440x900 demo set); the repo carries the same pixels compressed properly, next to
the preview page that displays them - roughly 200 KB a file. It is deliberately
LOSSLESS: these shots are the only record of what the UI looked like at a given
commit, and a palette reduction that "looks fine" is a rendering nobody can compare
against. Checking a re-encode is therefore exact and cheap:

    compare -metric RMSE build/shots/01-overview.png out/01-overview.png null:
    -> 0 (0)

An earlier version of this lived in /tmp and used a Pillow quantize pass; the scratch
dir was wiped, the encoder was gone, and the committed shots could no longer be
reproduced or trusted.

It lives here rather than in /tmp on purpose: the scratch dir gets wiped between
sandboxes, and a proof pipeline that silently disappears gets replaced by a stale
copy of the last committed shots, which is worse than having none.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

SETS = {
    "shots": ("build/shots", "out"),
    "menu": ("build/menuproof", "preview/img/menu"),
}


def encode(src_dir, dst_dir):
    if shutil.which("convert") is None:
        sys.exit("needs ImageMagick `convert` on PATH")
    src_dir = os.path.join(ROOT, src_dir)
    dst_dir = os.path.join(ROOT, dst_dir)
    if not os.path.isdir(src_dir):
        sys.exit("nothing to encode: %s does not exist - run the renderer first" % src_dir)
    os.makedirs(dst_dir, exist_ok=True)
    names = sorted(n for n in os.listdir(src_dir) if n.endswith(".png"))
    if not names:
        sys.exit("no PNGs in %s" % src_dir)
    for name in names:
        dst = os.path.join(dst_dir, name)
        subprocess.run(
            ["convert", os.path.join(src_dir, name), "-strip", "-depth", "8",
             "-define", "png:compression-level=9", "PNG24:" + dst],
            check=True)
        print("%-34s %7d" % (os.path.relpath(dst, ROOT), os.path.getsize(dst)))
    # Anything left over from an older shot set would outlive the set it proved.
    keep = set(names)
    for name in sorted(os.listdir(dst_dir)):
        if name.endswith(".png") and name not in keep:
            print("stale, not removed by this script: %s" % os.path.join(dst_dir, name))


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else ""
    if which not in SETS:
        sys.exit("usage: encode_shots.py [shots|menu]")
    encode(*SETS[which])
