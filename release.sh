#!/usr/bin/env bash
# Package both plugins as catalog-conformant release zips (mpc-vst-plugins tools/release.py), into dist/.
#   ./release.sh 0.1.0       (after ./build.sh)
set -euo pipefail
cd "$(dirname "$0")"
V="${1:?version, e.g. 0.1.0}"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
rm -rf dist && mkdir -p dist build/release
cp -r patches build/release/patches
mkdir -p build/release/licenses && cp LICENSE build/release/licenses/GPL-3.0.txt && cp art/fonts/OFL-*.txt build/release/licenses/
rel() {   # rel <id> <so> <gen dir> <name> <about>
  python3 "$MPC_VST/tools/release.py" --so "build/arm/$2" --skin "$3/skin/L'Cronx - VST - $4" --entry "$3/pluginlist-entry.xml" \
    --version "$V" --extra build/release/patches:patches --extra build/release/licenses:licenses \
    --user-data patches/user --about "$5" --id "$1" --repo sunskiefer/Meta4msXXL --license GPL-3.0-or-later -o dist
}
rel meta4msxxl meta4msxxl.so build "Meta4msXXL" "Plays 4ms MetaModule (VCV Rack) patches on the Force: every parameter on Q-Links, MIDI, clock, CV out, Wi-Fi upload from VCV Rack."
rel meta4msxxl-fx meta4msxxl_fx.so fx/build "Meta4msXXL FX" "Meta4msXXL as an effect: the track's audio into a 4ms MetaModule (VCV Rack) patch."
for z in dist/*.zip; do python3 "$MPC_VST/tools/catalog_check.py" "$z" --catalog; done
