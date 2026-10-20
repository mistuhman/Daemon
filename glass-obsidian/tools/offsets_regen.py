#!/usr/bin/env python3
"""Fold a Roblox offset dump into the app's offsets.h - and into the shipped fallback.

    python3 tools/offsets_regen.py /path/to/theo_offsets.h            # report only
    python3 tools/offsets_regen.py /path/to/theo_offsets.h --write     # rewrite + json

WHY THIS EXISTS
---------------
The dumper's header is NOT the file the app compiles against, and pasting one over the
other silently breaks three things at once. It happened on version-4310300497aa4917:

  1. `offsets.h` values are `inline uintptr_t`, deliberately NOT constexpr:
     RefreshOffsets() (offsets_live.cpp) takes the address of every one of them in
     kSlots[] and writes the live version's value into it. A constexpr paste makes that
     ill-formed - which is at least loud. Worse would be a paste that compiled and froze
     the values, because then a Roblox update needs a rebuild.
  2. The app reads ~20 fields the dumper does not emit at all (the second motion block
     inside Primitive, the MeshContentProvider/MeshData chain). A paste deletes them and
     three translation units stop compiling.
  3. The dumper answers 0x0 for fields it cannot resolve (RenderView's four,
     GuiBase2D::AbsoluteSize, MouseService::SensitivityPointer, ...). The app's own live
     parser treats a dump zero as "keep the compiled default" - so the header has to
     carry those defaults, and a paste turns them into reads at address+0.

So: take the dump, keep the app's shape. Everything the merge cannot know is printed as
a "needs a human" list rather than guessed at.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HDR  = os.path.join(ROOT, "roblox_external", "offsets.h")
JSON = os.path.join(ROOT, "roblox_external", "offsets_fallback.json")

FIELD_RE = re.compile(
    r"^(?P<pre>\s*inline (?:constexpr )?uintptr_t (?P<name>\w+)\s*=\s*)"
    r"(?P<val>0x[0-9a-fA-F]+|\d+)(?P<post>\s*;.*)$")
CLASS_RE = re.compile(r"^\s*namespace (\w+) \{\s*$")


def parse_header(text):
    """(ordered {class: {field: value}}, {class: [lines]], version)."""
    fields, order, cls, ver = {}, [], None, None
    for ln in text.split("\n"):
        m = CLASS_RE.match(ln)
        if m:
            cls = m.group(1)
            fields.setdefault(cls, {})
            if cls not in order:
                order.append(cls)
            continue
        if cls and re.match(r"^\s*\}\s*$", ln):
            cls = None
            continue
        if cls:
            m = FIELD_RE.match(ln)
            if m:
                fields[cls][m.group("name")] = int(m.group("val"), 0)
        v = re.search(r'ClientVersion\s*=\s*"(version-[0-9a-f]+)"', ln)
        if v and ver is None:
            ver = v.group(1)
        if ver is None:
            v = re.search(r"Roblox Version\s*:\s*(version-[0-9a-f]+)", ln)
            if v:
                ver = v.group(1)
    return fields, order, ver


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    write = "--write" in sys.argv
    if not args:
        sys.exit(__doc__)
    dump_text = io.open(args[0], encoding="utf-8", errors="replace").read()
    app_text = io.open(HDR, encoding="utf-8").read()

    dump, _, dump_ver = parse_header(dump_text)
    app, order, app_ver = parse_header(app_text)
    if not dump or dump_ver is None:
        sys.exit("that file has no `namespace X { inline ... }` offsets in it - "
                 "is it really a dumper header?")

    moved, kept_zero, app_only, dump_only = [], [], [], []
    for cls, flds in sorted(dump.items()):
        for f, v in sorted(flds.items()):
            if cls not in app or f not in app[cls]:
                dump_only.append((cls, f, v))
                continue
            old = app[cls][f]
            if v == 0 and old != 0:
                kept_zero.append((cls, f, old))
            elif v != old:
                moved.append((cls, f, old, v))
    for cls in sorted(app):
        for f in sorted(app[cls]):
            if cls not in dump or f not in dump[cls]:
                app_only.append((cls, f, app[cls][f]))

    # ---- the motion-block model, checked against the dump itself -----------------
    # The app writes BOTH physics blocks (see the comment in offsets.h), and the block-B
    # half is not something a dumper emits, so it is derived. The derivation is only
    # safe if the dump's own numbers still agree with the model, so ask them.
    derived = {}
    warn = []
    pr_app, pr_dump = app.get("Primitive", {}), dump.get("Primitive", {})
    def pick(f, default):
        return pr_dump.get(f) or pr_app.get(f, default)
    rot  = pick("Rotation", pr_app.get("Rotation", 0))
    pos  = pick("Position", 0)
    lin  = pick("AssemblyLinearVelocity", 0)
    ang  = pick("AssemblyAngularVelocity", 0)
    size = pr_app.get("MotionStride", 0x48)
    dpos, dlin, dang = (pr_app.get("BlockPosition", 0x24),
                        pr_app.get("BlockLinear", 0x30),
                        pr_app.get("BlockAngular", 0x3c))
    model = {"Position": dpos, "AssemblyLinearVelocity": dlin, "AssemblyAngularVelocity": dang}
    got = {"Position": pos - rot, "AssemblyLinearVelocity": lin - rot,
           "AssemblyAngularVelocity": ang - rot}
    if all(model[k] == got[k] for k in model):
        derived = {"MotionBlock": rot, "CFrame2": rot + size,
                   "Position2": rot + size + dpos, "AssemblyLinearVelocity2": rot + size + dlin,
                   "AssemblyAngularVelocity2": rot + size + dang}
    else:
        warn.append("Primitive's intra-block deltas moved (%s): the two-block model no "
                    "longer fits this dump. Block B is NOT re-derived - read the float "
                    "probe on a live client before trusting position writes." %
                    ", ".join("%s 0x%x (model 0x%x)" % (k, got[k], model[k])
                              for k in model if got[k] != model[k]))
        for k in ("Position", "Rotation", "AssemblyLinearVelocity", "AssemblyAngularVelocity"):
            warn.append("   dump says Primitive::%s = 0x%x" % (k, pr_dump.get(k, 0)))

    # ---- report ------------------------------------------------------------------
    print("dump version %s   app version %s" % (dump_ver, app_ver))
    print("values moved %d, dump-zero kept %d, app-only %d, dump-only %d, derived %d"
          % (len(moved), len(kept_zero), len(app_only), len(dump_only), len(derived)))
    for c, f, o, n in moved:
        print("  move    %-18s %-24s 0x%-9x -> 0x%-9x" % (c, f, o, n))
    for c, f, o in kept_zero:
        print("  keep    %-18s %-24s 0x%-9x (dump says 0x0)" % (c, f, o))
    for k, v in sorted(derived.items()):
        print("  derive  %-18s %-24s 0x%x" % ("Primitive", k, v))
    for c, f, o in app_only:
        print("  apponly %-18s %-24s 0x%x  (nothing in the dump to update it)" % (c, f, o))
    for c, f, v in dump_only:
        print("  new     %-18s %-24s 0x%x  (dump has it, the app does not read it)" % (c, f, v))
    for w in warn:
        print("  NEEDS A HUMAN  %s" % w)

    if not write:
        print("\n(nothing written - add --write)")
        return

    # ---- rewrite offsets.h: same text, new hex ----------------------------------
    out, cls, n_sub, n_tag = [], None, 0, 0
    for ln in app_text.split("\n"):
        m = CLASS_RE.match(ln)
        if m:
            cls = m.group(1)
            out.append(ln); continue
        if cls and re.match(r"^\s*\}\s*$", ln):
            cls = None
        mm = FIELD_RE.match(ln)
        if cls and mm:
            f = mm.group("name")
            new = None
            if f in derived and cls == "Primitive":
                new = derived[f]
            elif cls in dump and f in dump[cls] and dump[cls][f] != 0:
                new = dump[cls][f]
            if new is not None and new != int(mm.group("val"), 0):
                ln = "%s0x%x%s" % (mm.group("pre"), new, mm.group("post"))
                n_sub += 1
            # A kept zero is the one kind of stale value worth flagging in the file:
            # it is a value the app is using because nothing published one for it.
            if (cls in dump and f in dump[cls] and dump[cls][f] == 0
                    and app[cls][f] != 0 and "no value in the" not in ln):
                tag = "   // dump has no value; kept"
                ln = ln.rstrip().rstrip(";") + ";" + tag
                n_tag += 1
        out.append(ln)
    text = "\n".join(out)

    text = text.replace('inline std::string ClientVersion = "%s";' % app_ver,
                        'inline std::string ClientVersion = "%s";' % dump_ver)
    # The app's own rule first, the dumper's stamp second, so nobody has to rediscover
    # why this file is not a paste.
    lines = [
        '//  NOT A PASTE. Regenerate with tools/offsets_regen.py - a dumper header pasted',
        '//  over this one deletes the ~20 fields the app reads that no dumper emits, and',
        '//  its `constexpr` stops the live update from compiling.',
        '//',
        '//  Values below are the %s dump merged into the app\'s field list.' % dump_ver,
        '//  The list is longer than the dump and, unlike it, mutable: RefreshOffsets()',
        '//  writes these globals at launch (offsets_live.cpp takes their addresses in',
        '//  kSlots[]). A field the dump answers 0x0 for keeps the value in this file,',
        '//  because 0 is how the dumper says "not found" - the same rule the live parser',
        '//  applies, and the reason a pasted zero becomes a read at address+0. The raw',
        '//  dump for this version is docs/offsets_dump.txt. A line tagged "dump has no',
        '//  value; kept" is knowingly one release behind - the dumper publishes 0 for it,',
        '//  so this file keeps the previous cross-checked number - and the debug page',
        '//  names the version that is loaded, which is the only honest read on it.',
    ]
    prov = re.search(r"/\* =+\n(?:.*\n)*?\*/", dump_text)
    if prov:
        keep = []
        for ln in prov.group(0).split("\n"):
            for key in ("Dumped With", "Roblox Version", "Dumper Version", "Dumped At",
                        "Total Offsets"):
                if key in ln:
                    keep.append(ln.strip().lstrip("/*").strip())
                    break
        if keep:
            lines.append("//")
            lines.append("//  Source stamp from the pasted header:")
            lines += ["//    " + k for k in keep]
    note = "\n".join(lines) + "\n"
    head_end = text.index("namespace Offsets {")
    body = text[head_end:]
    text = ("#pragma once\n#include <cstdint>\n#include <string>\n" + note + "\n" + body)

    io.open(HDR, "w", encoding="utf-8").write(text)
    print("wrote %s (%d values updated, %d tagged as dump-zero keeps)"
          % (os.path.relpath(HDR, ROOT), n_sub, n_tag))

    # ---- the shipped fallback JSON, from the header that was just written ---------
    merged, order2, ver = parse_header(text)
    obj = {"Offsets": {c: {f: merged[c][f] for f in merged[c]} for c in order2}}
    n = sum(len(merged[c]) for c in order2)
    obj = {"Source": "bundled fallback - regenerated by tools/offsets_regen.py from "
                     "offsets.h for %s (imtheo dump, app field list)" % ver,
           "Roblox Version": ver,
           "Dumper Version": "merged",
           "Dumped With": "offsets_regen.py",
           "Dumped At": "regenerated from offsets.h",
           "Total Offsets": n,
           "Offsets": obj["Offsets"]}
    io.open(JSON, "w", encoding="utf-8").write(json.dumps(obj, separators=(",", ":")))
    print("wrote %s (%d offsets)" % (os.path.relpath(JSON, ROOT), n))


if __name__ == "__main__":
    main()
