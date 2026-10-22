**Meta4msXXL** (instrument) and **Meta4msXXL FX** (effect) play 4ms MetaModule patches — VCV Rack patches exported with the MetaModule hub — on the Akai Force (MPC OS 3.x, armv7), with 4ms's own open-source patch player and 215 built-in modules.

- Every parameter on Q-Link pages of eight: knob sets first, then every module (names and values from the modules)
- MIDI from the track and the Force's tempo/transport (MIDI clock) into the patch
- MIDI To Hub Ins: pads/keys to the hub's Gate and CV inputs
- CV Out: patch outs 3-8 as MIDI on the plugin's own port, for a Force CV track
- Wi-Fi Receive: send patches straight from the MetaModule hub in VCV Rack
- 24 factory patches; your own patches from a `Meta4msXXL` folder on internal storage, SD or USB

**Pre-release: not yet run on a Force.** Tested offline (x86: all 24 factory patches, every page and knob, state, Wi-Fi, the wrapper's host test) and under ARM emulation. Device checklist: TESTING.md.

Install: the zip's INSTALL.md (the installer stops and restarts MPC). Each plugin is an 11.7 MB download, about 16 MB installed.

Built on mpc-vst-plugins with the Q-Link/automation UpdateDisplay fix (#486-#489). GPL-3.0-or-later. Not affiliated with 4ms Company.
