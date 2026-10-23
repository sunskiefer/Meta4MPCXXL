# Testing Meta4MPCXXL

## Offline (x86, `test/run_tests.sh`) — 0.1.0, 2026-10-22

All passed:

- **Engine** (`test/test_engine.cc`): all 24 factory patches load with no missing module, play 3 s with notes and the
  host transport running (MIDI clock), output finite with no DC offset; every page selected and every knob on it
  moved (names and value text present). Tabs select pages (and ignore the wrapper's spring-back write). State round
  trip: patch, page, a changed knob and the level restored on a new instance. Wi-Fi: an upload built exactly as the VCV
  hub builds it (POST /action, FlatBuffers UploadPatch) is saved into `<drive>/Meta4MPCXXL/`, answered with a success
  Result, and loaded.
- **Wrapper host test** (mpc-vst-plugins `tools/host_test.c`, both plugins): two instances, names, set/get, option
  stepping by wheel and Q-Link, int stepping, no UpdateDisplay on 32 host writes (the #486-#489 fix), STOP toggle of
  parameter 0, chunk round trip, 3000 concurrent screen-side calls while rendering. Its two "rms 0" warnings are
  expected: it renders before the first patch has finished loading on the worker thread.

CPU on x86 (one core): 2-10% per patch; measure on the Force with `tools/bench.sh`.

**The Force build under ARM emulation** (the armv7 objects linked into the same engine test, run with qemu-arm on the
host): the first 4 factory patches, tabs, state round trip and Wi-Fi upload passed. This run found a real bug first:
32-bit `readdir()`/`stat()` failed on the host's filesystem and the patch folders looked empty; the file code is now
built with 64-bit offsets (`-D_FILE_OFFSET_BITS=64`). CPU figures under emulation mean nothing.

## On the Force (to do, in this order)

| # | Check | Result |
|---|---|---|
| 1 | Both plugins in the plugin list; insert Meta4MPCXXL on a track; the default patch plays from pads | |
| 2 | Skin: name in stencil, white text readable, knob colours per Q-Link, tabs and arrows | |
| 3 | Q-Links 1-8 move the knobs, Q-Link 9 turns pages; also on track edit -> automation | |
| 4 | Patch arrows load the next patch without clicks or dropouts | |
| 5 | Play/stop: a clocked patch (EnOsc step seq) follows the Force tempo | |
| 6 | MIDI To Hub Ins: Keys plays EnOscPoly4 and QuadDrum; Pads triggers QuadDrum from the first pad row | |
| 7 | Save and reload a project: same patch, same changed knobs | |
| 8 | Meta4MPCXXL FX on an audio track: audio into a patch's In 1/In 2 | |
| 9 | Wi-Fi Receive: address shown; VCV hub "send" loads the patch on the Force | |
| 10 | CV Out "Notes + CC": port appears in MPC's MIDI preferences; a CV track on it drives the CV/Gate jacks | |
| 11 | `tools/bench.sh` CPU per factory patch (PASS <= 15% average) | |
| 12 | Two instances at once, each with its own patch | |

Known limits: raw-MIDI modules (VCV's MIDI modules inside a patch) hear MIDI from every Meta4MPCXXL instance (4ms's
MIDI router is one per process); patches that use modules from MetaModule plugins not built in here (Bogaudio, etc.)
load with those modules missing, and the status line says so.
