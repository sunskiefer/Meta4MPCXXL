#!/usr/bin/env bash
# Meta4MPCXXL / Meta4MPCXXL FX -- GEN2 (aarch64) build: the same pipeline as
# build.sh, retargeted for MPC Gen2 devices (Live III / Key 61/37 / One+ /
# Force Gen2: RK3588, 64-bit userland, glibc 2.39; plugin links against
# glibc 2.31 as the ceiling). Zig cross-compiles; no Docker.
#   MPC_VST=<path> to use another mpc-vst-plugins checkout (default: bundled).
# Needs: python3 + Pillow + ziglang, a host gcc (shadow_art), binutils readelf.
# Output, one folder per plugin, ready for lab/vst/deploy-vst.sh:
#   build/package-gen2/L'Cronx - VST - Meta4MPCXXL/
#   build/package-gen2/L'Cronx - VST - Meta4MPCXXL FX/
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
TGT="-target aarch64-linux-gnu.2.31 -mcpu=generic -ffp-contract=off"

mkdir -p build/hosttools build/aarch64
[ -x build/hosttools/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/hosttools/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
python3 tools/make_art.py

# 1. params.h, skin and plugin-list entry for each plugin (arch-independent),
#    then our knobs and lettering
rm -rf build/skin fx/build/skin
for d in . fx; do
  SHADOW_ART="$PWD/build/hosttools/shadow_art" python3 "$MPC_VST/tools/gen_vst.py" "$d/vst.json"
done
python3 tools/knob_art.py "build/skin/L'Cronx - VST - Meta4MPCXXL/Plugin Skins"
python3 tools/post_skin.py "build/skin/L'Cronx - VST - Meta4MPCXXL/Plugin Skins" "Meta4MPCXXL"
python3 tools/knob_art.py "fx/build/skin/L'Cronx - VST - Meta4MPCXXL FX/Plugin Skins"
python3 tools/post_skin.py "fx/build/skin/L'Cronx - VST - Meta4MPCXXL FX/Plugin Skins" "Meta4MPCXXL FX"

# 2. the 4ms patch player + Meta4MPCXXL's engine, aarch64 (tools/build_engine.py target)
python3 tools/build_engine.py aarch64 -j "$(nproc)"

# 3. link each plugin: the shared engine objects, its own engine variant, the wrapper with its own params.h
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/aarch64/exports.map
link() {   # link <variant> <params.h dir> <so name>
  $ZIG cc $TGT -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -std=gnu11 -w -I"$2" -I"$MPC_VST/wrapper" \
    -c "$MPC_VST/wrapper/vst2_wrap.c" -o "build/aarch64/vst2_wrap_$1.o"
  $ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/aarch64/exports.map -Wl,--gc-sections -Wl,-s \
    $(cat build/aarch64/objects.txt) $(cat "build/aarch64/engine_$1.txt") "build/aarch64/vst2_wrap_$1.o" -lm -ldl -lpthread -o "build/aarch64/$3"
  readelf --dyn-syms -W "build/aarch64/$3" | awk '$5=="GLOBAL" && $7!="UND" {print "exported:", $8}' | grep -v '^exported: _' || true
  echo "  highest glibc: $(readelf -V "build/aarch64/$3" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (Gen2 MPC OS 3.x has 2.39)"
}
link inst build meta4mpcxxl.so
link fx fx/build meta4mpcxxl_fx.so

# 4. one folder per plugin: skin, .so, factory patches, licences
rm -rf build/package-gen2
for p in "Meta4MPCXXL:meta4mpcxxl.so:build" "Meta4MPCXXL FX:meta4mpcxxl_fx.so:fx/build"; do
  IFS=: read -r name so gen <<<"$p"
  PKG="build/package-gen2/L'Cronx - VST - $name"
  mkdir -p "$PKG/patches" "$PKG/licenses"
  cp -r "$gen/skin/L'Cronx - VST - $name/." "$PKG/"
  cp "build/aarch64/$so" "$PKG/"
  cp patches/*.yml "$PKG/patches/"
  cp LICENSE "$PKG/licenses/GPL-3.0.txt"
  cp art/fonts/OFL-*.txt "$PKG/licenses/"
  echo "-> $PKG"
done
md5sum build/package-gen2/*/*.so
