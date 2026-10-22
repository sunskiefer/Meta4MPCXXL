# Vendored: 4ms MetaModule patch player

Upstream: https://github.com/4ms/metamodule (GPL-3.0), commit `8c0922d8d88bc846ffe88f40adf5cd0558529cf2`,
with its submodules at the commits listed in `manifest.json` (`submodules`). Each project's own licence file is kept
next to its sources.

## What is here and why

Exactly the files 4ms's own **headless simulator** build compiles and includes (`simulator/README-headless.md`,
CMake preset `headless`): the patch player (`firmware/src/patch_play`), the patch file reader (`firmware/lib/patch-serial`,
rapidyaml), the 4ms core modules (`firmware/lib/CoreModules`), the VCV Rack API layer (`firmware/vcv_plugin`,
`firmware/metamodule-plugin-sdk`) and the built-in module brands in `firmware/vcv_ports` (Befaco, Mutable
Instruments / AudibleInstruments, HetrickCV, Nonlinear Circuits, Valley, eightfold, VCV core): 215 modules.

`tools/vendor_metamodule.py <checkout> <headless build dir>` produced this folder: it copies the sources from the
build's `compile_commands.json` and the headers from ninja's dependency log, keeping their paths, and writes
`manifest.json` with each source's defines, include dirs and standard. `tools/build_engine.py` compiles from that
manifest. To re-vendor from a newer upstream: build its headless simulator once with
`cmake --preset headless -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, run the script, and diff.

Also vendored, next to this folder:
- `third_party/4ms-vcv/flat/all_generated.h`: the FlatBuffers schema of the hub's Wi-Fi upload, from
  https://github.com/4ms/4ms-vcv (GPL-3.0) commit `a60049a442a3a6ad7e3dae43dbc1b3b1ffd78abc`.
- `third_party/flatbuffers/include`: FlatBuffers 23.5.26 headers (Apache-2.0), commit
  `0100f6a5779831fa7a651e4b67ef389a8752bd9b` (the version the schema header requires).

## Local changes

No vendored file is edited. Differences from 4ms's build:

1. `simulator/stubs/async_thread_control.cc` is replaced by `src/replaced/async_thread_control.cc` (same API):
   fixed task slots that never move, a lock around create/destroy, a reference-counted runner thread. Several plugin
   instances load and unload patches concurrently inside MPC, which the simulator stub was not written for.
2. Built with `LOG_LEVEL=1` (errors only) instead of 4 (debug), on every unit.
3. On the Force (armv7): `-mcpu=cortex_a17` (NEON) and `-U__ARM_FEATURE_DSP`, so CMSIS-DSP takes its plain C paths
   (the ones 4ms's x86 simulator runs); its DSP-extension paths need CMSIS-Core intrinsics that are not part of
   this build.
4. The simulator's `main-headless.cc` and wav-file helpers are not built; `src/engine.cc` takes their place.
