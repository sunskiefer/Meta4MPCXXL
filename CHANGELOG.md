# Changelog

## 0.1.2 (2026-10-23)
- Fixed: the installer stopped with `install.sh: line 30: syntax error: unexpected "("` and nothing was installed.
  The ' in L'Cronx broke the installer script; it is now escaped (mpc-vst-plugins with the L'Cronx quoting fix), and the
  release fails if any installer script doesn't parse.

## 0.1.1 (2026-10-23)
- Renamed from Meta4msXXL to **Meta4MPCXXL** (and Meta4MPCXXL FX): no company name in the plugin's title. New plugin
  IDs (M4Pi / M4Pf), so it installs as a new plugin. Your own patches now go in a folder named **Meta4MPCXXL**.
- Not yet run on a Force: offline x86 tests and an ARM emulation run (TESTING.md). Released as a pre-release.

## 0.1.0 (2026-10-22)
First build: Meta4MPCXXL (instrument) and Meta4MPCXXL FX (effect) for the Akai Force / MPC OS (armv7).
- 4ms MetaModule patch player (their firmware's own, 215 built-in modules), 24 factory patches.
- Every parameter on Q-Link pages of eight: knob sets first, then every module; names and values from the modules.
- MIDI from the track into the patch, the Force's tempo and transport as MIDI clock.
- MIDI To Hub Ins (Keys / Pads) for patches played from gates and CV.
- CV Out: patch outs 3-8 on the plugin's own MIDI port, for a Force CV track.
- Wi-Fi Receive: VCV Rack's MetaModule hub sends patches straight to the Force.
- Patches from a Meta4MPCXXL folder on internal storage, SD or USB; projects keep the patch and every changed knob.
- Panel: black, Davies 1900H-style knobs (one colour per Q-Link), the name in Saira Stencil One, all text large and white.
- mpc-vst-plugins wrapper with the Q-Link/automation UpdateDisplay fix (#486-#489).
- Not yet run on a Force: offline x86 tests and an ARM emulation run (TESTING.md). Released as a pre-release.
