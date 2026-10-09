#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

"""Re-vendor the SCUMM engine from a fruitjam-arcade checkout.

    revendor.py /path/to/fruitjam-arcade

Copies ports/scumm/src into src/machines/scumm/core/ and makes the three
mechanical changes an Arduino library needs (src/machines/scumm/core/
VENDORED.md has the why):

1. Leaves out the CircuitPython-only pieces: src/modscumm.c and src/libc/.
2. Renames the two .cpp files whose names repeat elsewhere in the engine
   (dot_a_linkage archives need unique object names, DEVNOTES #126):
   scumm/file.cpp -> scumm/scumm_file.cpp, scumm/util.cpp -> scumm/scumm_util.cpp.
3. Rewrites every quoted include that names a file in the engine tree
   ("common/array.h", "scumm/costume.h", ...) to a path relative to the
   including file, since only the library's src/ is on the include path,
   and adds `#include ".../backend/fj_arduino.h"` as the first line of
   every .c/.cpp, which stands in for the host build's -DFJ_HOST and
   -include fj_host_arena.h.

Then it applies this directory's patches (src/machines/scumm/core/*.patch,
in name order, `patch -p1`), which VENDORED.md lists. Everything else is
byte-for-byte upstream. The licence files (LICENSE,
AUTHORS, COPYRIGHT) are copied from ports/scumm/. fj_arduino.h,
VENDORED.md and this script are ours and are not touched.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
DEST = os.path.join(ROOT, "src", "machines", "scumm", "core")
KEEP = {"VENDORED.md", os.path.join("backend", "fj_arduino.h")}
SKIP_DIRS = {"libc"}
SKIP_FILES = {"modscumm.c"}
RENAMES = {
    os.path.join("scumm", "file.cpp"): os.path.join("scumm", "scumm_file.cpp"),
    os.path.join("scumm", "util.cpp"): os.path.join("scumm", "scumm_util.cpp"),
}
LICENCE_FILES = ["LICENSE", "AUTHORS", "COPYRIGHT"]
INCLUDE = re.compile(r'^(\s*#\s*include\s*)"([^"]+)"', re.M)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    repo = os.path.abspath(sys.argv[1])
    src = os.path.join(repo, "ports", "scumm", "src")
    commit = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"],
                            capture_output=True, text=True, check=True).stdout.strip()

    # Clear everything but our own files.
    for dirpath, dirnames, filenames in os.walk(DEST, topdown=False):
        for f in filenames:
            rel = os.path.relpath(os.path.join(dirpath, f), DEST)
            if rel not in KEEP and not rel.endswith(".patch"):
                os.remove(os.path.join(dirpath, f))
        if dirpath != DEST and not os.listdir(dirpath):
            os.rmdir(dirpath)

    # Copy.
    copied = []
    for dirpath, dirnames, filenames in os.walk(src):
        rel_dir = os.path.relpath(dirpath, src)
        if rel_dir.split(os.sep)[0] in SKIP_DIRS:
            continue
        for f in filenames:
            rel = os.path.normpath(os.path.join(rel_dir, f))
            if rel in SKIP_FILES:
                continue
            out = RENAMES.get(rel, rel)
            os.makedirs(os.path.join(DEST, os.path.dirname(out)), exist_ok=True)
            shutil.copyfile(os.path.join(dirpath, f), os.path.join(DEST, out))
            copied.append(out)
    for f in LICENCE_FILES:
        shutil.copyfile(os.path.join(repo, "ports", "scumm", f), os.path.join(DEST, f))

    # Rewrite includes, and add the build-mode header to each source file.
    tree = set(copied)
    rewritten = 0
    for rel in copied:
        if not rel.endswith((".c", ".cpp", ".h")):
            continue
        path = os.path.join(DEST, rel)
        here = os.path.dirname(rel)
        text = open(path, encoding="latin-1").read()

        def fix(m):
            nonlocal rewritten
            target = os.path.normpath(m.group(2))
            if target in tree:  # named from the engine root
                new = os.path.relpath(target, here or ".")
                if new != m.group(2):
                    rewritten += 1
                    return '%s"%s"' % (m.group(1), new.replace(os.sep, "/"))
            return m.group(0)

        text = INCLUDE.sub(fix, text)
        if rel.endswith((".c", ".cpp")):
            cfg = os.path.relpath(os.path.join("backend", "fj_arduino.h"), here or ".")
            text = '#include "%s" // Adafruit Arcade Machines build mode\n' % cfg.replace(os.sep, "/") + text
        open(path, "w", encoding="latin-1").write(text)

    patches = sorted(f for f in os.listdir(DEST) if f.endswith(".patch"))
    for p in patches:
        subprocess.run(["patch", "-p1", "-s", "-d", DEST, "-i", os.path.join(DEST, p)], check=True)
    print("vendored %d files from %s, %d includes rewritten, %d patch(es) applied"
          % (len(copied), commit, rewritten, len(patches)))
    print("now update the commit in %s" % os.path.join(DEST, "VENDORED.md"))


if __name__ == "__main__":
    main()
