#!/usr/bin/env python3
"""Vendor the parts of 4ms MetaModule that the patch player needs into third_party/metamodule/.

    python3 tools/vendor_metamodule.py <metamodule checkout> <its headless build dir>

The checkout is https://github.com/4ms/metamodule with submodules, configured and built once with its own
`headless` CMake preset plus -DCMAKE_EXPORT_COMPILE_COMMANDS=ON (simulator/README-headless.md). That build is
the reference: this script copies exactly the sources it compiled and the headers they included (ninja's
dependency log), keeping their paths, and writes third_party/metamodule/manifest.json with each source's own
defines, include dirs, language standard and forced includes. tools/build_engine.py compiles from that manifest,
so the plugin builds the same code the 4ms simulator does, without CMake and without fetching anything.

Left out: the simulator's main (src/headless/main-headless.cc) and its wav file helpers; Meta4msXXL's engine
(src/) takes their place. Licence files of every vendored project are copied too.
"""
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
DEST = os.path.join(ROOT, "third_party", "metamodule")

SKIP_SOURCES = ("simulator/src/headless/main-headless.cc", "simulator/src/headless/audio_files.cc")
# Replaced by Meta4msXXL's own versions (see third_party/metamodule/VENDORED.md)
REPLACED = ("simulator/stubs/async_thread_control.cc",)
# Headers Meta4msXXL's engine uses that the headless simulator itself did not include
EXTRA_FILES = ("firmware/src/gui/elements/state_names.hh", "firmware/src/core_m4/midi_controls.hh",
               "firmware/src/audio/midi.hh")
KEEP_FLAGS = re.compile(r"^-(std=|D|include$|f(no-)?(fast-math|unsafe-math-optimizations|strict-aliasing))")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    mm = os.path.abspath(sys.argv[1])
    build = os.path.abspath(sys.argv[2])
    cc = json.load(open(os.path.join(build, "compile_commands.json")))

    deps = subprocess.run(["ninja", "-C", build, "-t", "deps"], capture_output=True, text=True, check=True).stdout
    used = {l.strip() for l in deps.splitlines() if l.startswith("    /")}

    def rel(p):
        p = os.path.normpath(p if os.path.isabs(p) else os.path.join(build, p))
        if p.startswith(mm + "/"):
            return p[len(mm) + 1:]
        if p.startswith(build + "/"):
            return "generated/" + p[len(build) + 1:]
        return None

    units = []
    files = set()
    for e in cc:
        src = rel(e["file"])
        if src is None or src in SKIP_SOURCES:
            continue
        args = shlex.split(e["command"])
        flags, incs = [], []
        i = 1
        while i < len(args):
            a = args[i]
            if a in ("-o", "-c", "-MF", "-MT", "-MQ"):
                i += 2
                continue
            if a.startswith("-I") or a == "-isystem":
                d = a[2:] if a.startswith("-I") else args[i + 1]
                if a == "-isystem":
                    i += 1
                r = rel(d)
                if r is not None:
                    incs.append(r)
            elif a == "-include":
                r = rel(args[i + 1])
                flags += ["-include", r]
                files.add(r)
                i += 1
            elif KEEP_FLAGS.match(a):
                flags.append(a)
            i += 1
        lang = "c" if src.endswith(".c") else "asm" if src.endswith((".s", ".S")) else "c++"
        units.append({"src": src, "lang": lang, "flags": flags, "inc": incs})
        if src not in REPLACED:
            files.add(src)

    files.update(EXTRA_FILES)
    for h in used:
        r = rel(h)
        if r is not None:
            files.add(r)

    if os.path.isdir(DEST):
        for name in os.listdir(DEST):
            if name not in ("VENDORED.md",):
                p = os.path.join(DEST, name)
                shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
    os.makedirs(DEST, exist_ok=True)

    n = 0
    for r in sorted(files):
        s = os.path.join(build, r[len("generated/"):]) if r.startswith("generated/") else os.path.join(mm, r)
        if not os.path.isfile(s):
            continue
        d = os.path.join(DEST, r)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)
        n += 1

    # licences of every project a vendored file comes from
    lic = re.compile(r"^(LICEN[SC]E|COPYING|LICENSE[-_.].*|LICENSE\..*)$", re.I)
    roots = set()
    for r in files:
        parts = r.split("/")
        for k in range(1, len(parts)):
            roots.add("/".join(parts[:k]))
    nl = 0
    for d in sorted(roots):
        src_dir = os.path.join(mm, d)
        if not os.path.isdir(src_dir):
            continue
        for f in os.listdir(src_dir):
            if lic.match(f) and os.path.isfile(os.path.join(src_dir, f)):
                os.makedirs(os.path.join(DEST, d), exist_ok=True)
                shutil.copy2(os.path.join(src_dir, f), os.path.join(DEST, d, f))
                nl += 1

    commit = subprocess.run(["git", "-C", mm, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    subs = subprocess.run(["git", "-C", mm, "submodule", "status", "--recursive"], capture_output=True, text=True).stdout
    json.dump({"upstream": "https://github.com/4ms/metamodule", "commit": commit,
               "submodules": [l.split()[:2] for l in subs.splitlines() if l.strip()],
               "units": units}, open(os.path.join(DEST, "manifest.json"), "w"), indent=1)
    print("vendored %d files, %d licence files, %d compile units, upstream %s" % (n, nl, len(units), commit[:12]))


if __name__ == "__main__":
    main()
