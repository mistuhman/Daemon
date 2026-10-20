#!/usr/bin/env python3
"""Small source upgrade, shipped by the existing frozen courier; no new service."""
import hashlib
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
files = [
    'roblox_external/platform/sober_scanner.cpp',
    'roblox_external/platform/sober_scanner.h',
    'roblox_external/platform/platform_linux.cpp',
    'roblox_external/platform/main_linux.cpp',
    'roblox_external/game.cpp',
    'roblox_external/menu_linux.cpp',
    'roblox_external/offsets_dynamic.cpp',
    'boot/bootstrap_core.cpp',
    'roblox_external/platform/linux_elf.h',
]
new_path = files[-1]
lines = ['''#!/bin/sh
# Source-verified Linux attachment upgrade. Uses existing setup/build dependencies.
set -eu
[ "$#" -eq 1 ] || { echo "Usage: sh upgrade.txt EXTRACTED_PAYLOAD_DIRECTORY" >&2; exit 2; }
DEST=$(CDPATH= cd -- "$1" && pwd)
[ -f "$DEST/CMakeLists.txt" ] && [ -f "$DEST/setup.sh" ] || {
 echo "Expected an extracted Linux payload source tree, not the installed bin directory." >&2; exit 2;
}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/sober-upgrade.XXXXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
''']
manifest = []
for index, name in enumerate(files):
    content = (root / name).read_text()
    if name == new_path:
        before = '-'
    else:
        original = subprocess.check_output(['git', 'show', f'c6655cf:{name}'], cwd=root)
        before = hashlib.sha256(original).hexdigest()
    after = hashlib.sha256(content.encode()).hexdigest()
    delimiter = f'SOBER_SOURCE_{index}_END'
    assert delimiter not in content
    lines += [f'mkdir -p "$WORK/{pathlib.PurePosixPath(name).parent}"\n',
              f'cat > "$WORK/{name}" <<\'{delimiter}\'\n', content, delimiter + '\n']
    manifest.append(f'{name} {before} {after}')
lines += ['cat > "$WORK/manifest" <<\'SOBER_MANIFEST_END\'\n',
          '\n'.join(manifest) + '\nSOBER_MANIFEST_END\n', '''# Validate every preimage and staged source before changing any target file.
while read -r path before after; do
 actual=$(sha256sum "$WORK/$path" | cut -d' ' -f1)
 [ "$actual" = "$after" ] || { echo "Staged source mismatch: $path" >&2; exit 1; }
 [ ! -L "$DEST/$path" ] || { echo "Refusing symlink: $path" >&2; exit 1; }
 if [ -e "$DEST/$path" ]; then
   actual=$(sha256sum "$DEST/$path" | cut -d' ' -f1)
   [ "$actual" = "$after" ] || [ "$actual" = "$before" ] || {
     echo "Unsupported or locally modified source: $path; nothing changed." >&2; exit 1;
   }
 else
   [ "$before" = '-' ] || { echo "Missing source: $path; nothing changed." >&2; exit 1; }
 fi
done < "$WORK/manifest"
while read -r path before after; do
 cp "$WORK/$path" "$DEST/$path"
done < "$WORK/manifest"
echo "SOBER_ATTACHMENT_UPGRADE_022=APPLIED"
echo "Verified ELF attachment replaces name scores; unvalidated game features remain disabled."
echo "Next: sh $DEST/setup.sh"
''']
out = pathlib.Path(sys.argv[1])
out.write_text(''.join(lines))
print(hashlib.sha256(out.read_bytes()).hexdigest(), out)
