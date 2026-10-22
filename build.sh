#!/usr/bin/env bash
# Build Meta4msXXL and Meta4msXXL FX for MPC OS Gen1 / Akai Force (armv7-a hard-float, NEON, glibc <= 2.31).
#   ./build.sh            (MPC_VST=<path> to use another mpc-vst-plugins checkout)
# Needs: python3 + Pillow, a host gcc (for the skin renderer and the offline tests), and Zig (pip install ziglang).
# No Docker. Output, one folder per plugin, ready for tools/release.py:
#   build/package/L'Cronx - VST - Meta4msXXL/      (skin, meta4msxxl.so, patches/)
#   build/package/L'Cronx - VST - Meta4msXXL FX/   (skin, meta4msxxl_fx.so, patches/)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17"

mkdir -p build/hosttools build/arm
[ -x build/hosttools/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/hosttools/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
python3 tools/make_art.py

# 1. params.h, skin and plugin-list entry for each plugin, then our knobs and lettering
rm -rf build/skin fx/build/skin
for d in . fx; do
  SHADOW_ART="$PWD/build/hosttools/shadow_art" python3 "$MPC_VST/tools/gen_vst.py" "$d/vst.json"
done
python3 tools/knob_art.py "build/skin/L'Cronx - VST - Meta4msXXL/Plugin Skins"
python3 tools/post_skin.py "build/skin/L'Cronx - VST - Meta4msXXL/Plugin Skins" "Meta4msXXL"
python3 tools/knob_art.py "fx/build/skin/L'Cronx - VST - Meta4msXXL FX/Plugin Skins"
python3 tools/post_skin.py "fx/build/skin/L'Cronx - VST - Meta4msXXL FX/Plugin Skins" "Meta4msXXL FX"

# 2. the 4ms patch player + Meta4msXXL's engine, for the Force
python3 tools/build_engine.py arm

# 3. link each plugin: the shared engine objects, its own engine variant, the wrapper with its own params.h
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
link() {   # link <variant> <params.h dir> <so name>
  $ZIG cc $TGT -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -std=gnu11 -w -I"$2" -I"$MPC_VST/wrapper" \
    -c "$MPC_VST/wrapper/vst2_wrap.c" -o "build/arm/vst2_wrap_$1.o"
  $ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
    $(cat build/arm/objects.txt) $(cat "build/arm/engine_$1.txt") "build/arm/vst2_wrap_$1.o" -lm -ldl -lpthread -o "build/arm/$3"
  echo "$3: $(du -h "build/arm/$3" | cut -f1), exports: $(readelf --dyn-syms -W "build/arm/$3" | awk '$5=="GLOBAL" && $7!="UND" {print $8}' | tr '\n' ' ')"
  echo "  highest glibc: $(readelf -V "build/arm/$3" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
}
link inst build meta4msxxl.so
link fx fx/build meta4msxxl_fx.so

# 4. one folder per plugin: skin, .so, factory patches, licences
rm -rf build/package
for p in "Meta4msXXL:meta4msxxl.so:build" "Meta4msXXL FX:meta4msxxl_fx.so:fx/build"; do
  IFS=: read -r name so gen <<<"$p"
  PKG="build/package/L'Cronx - VST - $name"
  mkdir -p "$PKG/patches" "$PKG/licenses"
  cp -r "$gen/skin/L'Cronx - VST - $name/." "$PKG/"
  cp "build/arm/$so" "$PKG/"
  cp patches/*.yml "$PKG/patches/"
  cp LICENSE "$PKG/licenses/GPL-3.0.txt"
  cp art/fonts/OFL-*.txt "$PKG/licenses/"
  echo "-> $PKG"
done
