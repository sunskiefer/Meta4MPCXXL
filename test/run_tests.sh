#!/usr/bin/env bash
# Offline x86 tests for Meta4msXXL (run before anything goes to the Force):
#   1. test/test_engine.cc: every factory patch loaded, played (MIDI notes + host clock), every page and knob moved,
#      state round trip, Wi-Fi upload as the VCV hub sends it;
#   2. mpc-vst-plugins' own wrapper host test (tools/host_test.c) on both plugins: instances, names, set/get,
#      option select and nudge, stepping, MIDI -> audio, chunk round trip, no UpdateDisplay on host writes.
# Needs the host build: python3 tools/build_engine.py host
set -euo pipefail
cd "$(dirname "$0")/.."
MV=third_party/mpc-vst-plugins
python3 tools/build_engine.py host
OBJS=$(cat build/host/objects.txt)
INC="-I$MV/wrapper -Ithird_party/flatbuffers/include -Ithird_party/4ms-vcv"
mkdir -p build/test
g++ -std=gnu++23 -O1 -g $INC -c test/test_engine.cc -o build/test/test_engine.o
g++ -o build/test/test_engine build/test/test_engine.o $(cat build/host/engine_inst.txt) $OBJS -lpthread -ldl
build/test/test_engine "$PWD"

for v in inst fx; do
  dir=.; [ $v = fx ] && dir=fx
  python3 $MV/tools/gen_vst.py $dir/vst.json --params-h >/dev/null
  gcc -std=gnu11 -O1 -g -I$dir/build -c $MV/tools/host_test.c -o build/test/host_test_$v.o
  gcc -std=gnu11 -O1 -g -I$dir/build -c $MV/wrapper/vst2_wrap.c -o build/test/vst2_wrap_$v.o
  g++ -o build/test/host_test_$v build/test/host_test_$v.o build/test/vst2_wrap_$v.o $(cat build/host/engine_$v.txt) $OBJS -lpthread -ldl
  ln -sfn ../../patches build/test/patches   # MODULE_SUBDIR: the factory patches next to the plugin
  echo "== wrapper host test: $v"
  (cd build/test && ./host_test_$v)
done
