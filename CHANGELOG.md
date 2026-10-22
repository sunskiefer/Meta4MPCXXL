# Changelog

## 0.1.0 (2026-10-22)
First build: Meta4msXXL (instrument) and Meta4msXXL FX (effect) for the Akai Force / MPC OS (armv7).
- 4ms MetaModule patch player (their firmware's own, 215 built-in modules), 24 factory patches.
- Every parameter on Q-Link pages of eight: knob sets first, then every module; names and values from the modules.
- MIDI from the track into the patch, the Force's tempo and transport as MIDI clock.
- MIDI To Hub Ins (Keys / Pads) for patches played from gates and CV.
- CV Out: patch outs 3-8 on the plugin's own MIDI port, for a Force CV track.
- Wi-Fi Receive: VCV Rack's MetaModule hub sends patches straight to the Force.
- Patches from a Meta4msXXL folder on internal storage, SD or USB; projects keep the patch and every changed knob.
- Panel: black, Davies 1900H-style knobs (one colour per Q-Link), the name in Saira Stencil One, all text large and white.
- mpc-vst-plugins wrapper with the Q-Link/automation UpdateDisplay fix (#486-#489).
