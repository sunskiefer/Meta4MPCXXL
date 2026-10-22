#!/usr/bin/env python3
"""Compile the vendored MetaModule patch player plus Meta4msXXL's engine into object files.

    python3 tools/build_engine.py arm   [-j N]   -> build/arm/obj/...   (Force / MPC OS Gen1: armv7-a hard-float, glibc 2.31, via Zig)
    python3 tools/build_engine.py host  [-j N]   -> build/host/obj/...  (x86 test build, the host's g++/gcc)

Every MetaModule unit is compiled with the defines, include dirs, standard and forced includes recorded in
third_party/metamodule/manifest.json (taken from 4ms's own headless build, tools/vendor_metamodule.py), so the code
is built the way 4ms builds it. Meta4msXXL's own sources (ENGINE_SOURCES below) get the union of MetaModule's
include dirs. Objects are rebuilt only when the source or any header it included changed (-MMD depfiles).
Prints the list of object files (one per line) on success; build.sh links them with the wrapper.
"""
import concurrent.futures as cf
import hashlib
import json
import os
import shlex
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MM = "third_party/metamodule"
ENGINE_SOURCES = ["src/pages.cc"]
# Meta4msXXL sources that use only the C library and POSIX: compiled WITHOUT MetaModule's include dirs, whose plugin
# SDK has its own dirent.h/stdio replacements for the hardware (they shadowed opendir() and the library came up empty).
PLAIN_SOURCES = ["src/library.cc", "src/cv_port.cc", "src/wifi.cc"]
# compiled once per plugin: Meta4msXXL (instrument) and Meta4msXXL FX (effect)
VARIANTS = {"inst": [], "fx": ["-DMETA4MS_FX"]}
# Meta4msXXL's own sources take the flags 4ms used for the patch player itself, so every header they share is
# compiled the same way on both sides.
TEMPLATE_UNIT = "firmware/src/patch_play/patch_player_load.cc"
EXTRA_INCLUDES = ["src", "third_party/flatbuffers/include", "third_party/4ms-vcv"]

ZIG = os.environ.get("ZIG", "python3 -m ziglang").split()
TARGETS = {
    "arm": {
        "cc": ZIG + ["cc", "-target", "arm-linux-gnueabihf.2.31", "-mcpu=cortex_a17"],
        "cxx": ZIG + ["c++", "-target", "arm-linux-gnueabihf.2.31", "-mcpu=cortex_a17"],
        "common": ["-fPIC", "-fvisibility=hidden", "-fvisibility-inlines-hidden", "-ffunction-sections",
                   "-fdata-sections", "-w", "-ffp-contract=off",
                   # CMSIS-DSP's DSP-extension paths need CMSIS-Core intrinsics (__QADD...) the simulator never
                   # vendored; its plain C paths are what 4ms's x86 build runs, so take those.
                   "-U__ARM_FEATURE_DSP"],
    },
    "host": {
        "cc": ["gcc"],
        "cxx": ["g++"],
        "common": ["-fPIC", "-fvisibility=hidden", "-fvisibility-inlines-hidden", "-w", "-g1"],
    },
}


def opt_for(target, src):
    # MetaModule builds everything with -O3 -ffast-math; so do we.
    return ["-O3", "-ffast-math", "-funsafe-math-optimizations"]


def fix_flag(f):
    # 4ms's headless build logs at debug level; inside MPC only errors are wanted (LOG_LEVEL is macros only).
    return "-DLOG_LEVEL=1" if f.startswith("-DLOG_LEVEL=") else f


def depfile_stale(obj, dep):
    if not (os.path.exists(obj) and os.path.exists(dep)):
        return True
    t = os.path.getmtime(obj)
    text = open(dep).read().replace("\\\n", " ")
    files = shlex.split(text.split(":", 1)[1]) if ":" in text else []
    for f in files:
        p = f if os.path.isabs(f) else os.path.join(ROOT, f)
        if not os.path.exists(p) or os.path.getmtime(p) > t:
            return True
    return False


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "arm"
    jobs = os.cpu_count() or 2
    if "-j" in sys.argv:
        jobs = int(sys.argv[sys.argv.index("-j") + 1])
    tc = TARGETS[target]
    man = json.load(open(os.path.join(ROOT, MM, "manifest.json")))
    out = os.path.join("build", target, "obj")

    tmpl = next(u for u in man["units"] if u["src"] == TEMPLATE_UNIT)

    jobs_list = []
    for u in ([] if "--engine-only" in sys.argv else man["units"]):
        src = u["src"]
        local = os.path.join("src", "replaced", os.path.basename(src))
        path = local if os.path.exists(os.path.join(ROOT, local)) else os.path.join(MM, src)
        flags = []
        it = iter(u["flags"])
        for f in it:
            if f == "-include":
                flags += ["-include", os.path.join(MM, next(it))]
            elif f.startswith(("-ffast-math", "-funsafe")):
                continue
            else:
                flags.append(fix_flag(f))
        incs = ["-I" + os.path.join(MM, i) for i in u["inc"]]
        jobs_list.append((path, u["lang"], flags, incs))
    variant_jobs = {}
    for s in ([] if "--mm-only" in sys.argv else ENGINE_SOURCES + ["src/engine.cc@" + v for v in VARIANTS]):
        extra = []
        s0 = s
        if "@" in s:
            s, v = s.split("@")
            extra = VARIANTS[v]
        incs = ["-I" + i for i in EXTRA_INCLUDES] + ["-I" + os.path.join(MM, i) for i in tmpl["inc"]] + \
               ["-I" + os.environ.get("MPC_VST", "third_party/mpc-vst-plugins") + "/wrapper"]
        flags = [fix_flag(f) for f in tmpl["flags"] if not f.startswith(("-ffast-math", "-funsafe"))]
        jobs_list.append((s + ("@" + v if "@" in s0 else ""), "c++", flags + extra, incs))

    def compile_one(job):
        path, lang, flags, incs = job
        # the object name carries a hash of the source AND its flags, so a flag change rebuilds it
        h = hashlib.sha1((path + " " + " ".join(flags + incs)).encode()).hexdigest()[:8]
        path = path.split("@")[0]
        obj = os.path.join(out, "%s_%s.o" % (os.path.basename(path).replace(".", "_"), h))
        dep = obj[:-2] + ".d"
        if not depfile_stale(os.path.join(ROOT, obj), os.path.join(ROOT, dep)):
            return obj, None
        cmd = (tc["cc"] if lang == "c" else tc["cxx"]) + opt_for(target, path) + tc["common"] + flags + incs + \
              ["-MMD", "-MF", dep, "-c", path, "-o", obj]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            return obj, "%s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr[-6000:])
        return obj, None

    for s in PLAIN_SOURCES:
        # 64-bit file offsets and inode numbers: 32-bit readdir()/stat() fail (EOVERFLOW) on filesystems with large
        # inode numbers, and the patch folder then looked empty (found under ARM emulation)
        jobs_list.append((s, "c++", ["-std=gnu++23", "-DNDEBUG", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE64_SOURCE"],
                          ["-I" + i for i in EXTRA_INCLUDES]))
    # rapidyaml's c4core is listed twice in 4ms's build (ryml's own copy and its subproject): compile each file once
    seen, uniq = set(), []
    for j in jobs_list:
        if j[0] not in seen:
            seen.add(j[0])
            uniq.append(j)
    jobs_list = uniq
    os.makedirs(os.path.join(ROOT, out), exist_ok=True)
    objs, errors = [], []
    with cf.ThreadPoolExecutor(jobs) as ex:
        for obj, err in ex.map(compile_one, jobs_list):
            objs.append(obj)
            if err:
                errors.append(err)
                sys.stderr.write("FAILED: " + err + "\n")
    if errors:
        sys.exit("%d unit(s) failed" % len(errors))
    shared = [o for o, j in zip(objs, jobs_list) if "@" not in j[0]]
    if "--engine-only" in sys.argv:
        print("engine compiled")
        return
    open(os.path.join(ROOT, "build", target, "objects.txt"), "w").write("\n".join(shared) + "\n")
    for o, j in zip(objs, jobs_list):
        if "@" in j[0]:
            open(os.path.join(ROOT, "build", target, "engine_%s.txt" % j[0].split("@")[1]), "w").write(o + "\n")
    print("%d objects in %s" % (len(objs), out))


if __name__ == "__main__":
    main()
